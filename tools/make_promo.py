#!/usr/bin/env python3
"""
ReplicantTR8S - Hard Techno promo video generator (1920x1080 @ 30fps, English).

Builds the full soundtrack (TR-8S drum stem from the actual plugin DSP + acid bassline
& dark cinematic techno bed) and renders a 1920x1080 / 30 fps Blade-Runner-styled
promo video showcasing the new Per-Channel Reverb & Delay controls (between Decay and Ctrl)
and the new Anti-Saturation 150 Max Mixer Ceiling starting at max level.

Outputs:
    assets/replicant_tr8s_promo_hardtechno_1080p.mp4
    assets/replicant_tr8s_promo_hardtechno_poster.png
"""

import math
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

W, H = 1920, 1080
FPS = 30
DURATION = 64.0
SAMPLE_RATE = 48000

AMBER = (255, 184, 92)
GREEN = (0, 230, 91)
CYAN = (92, 226, 255)
RED = (255, 54, 66)
WHITE = (232, 240, 238)
MUTED = (150, 168, 168)

FONT_TITLE = "/usr/share/fonts/TTF/RobotoCondensed-Bold.ttf"
FONT_MONO = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Bold.ttf"
FONT_MONO_REG = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf"

_cache = {}


def font(path, size):
    key = (path, size)
    if key not in _cache:
        _cache[key] = ImageFont.truetype(path, size)
    return _cache[key]


def load_wav(path, n):
    with wave.open(path, "rb") as w:
        ch = w.getnchannels()
        sw = w.getsampwidth()
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8)

    if sw == 3:
        b = raw.reshape(-1, 3)
        v = (b[:, 0].astype(np.int32)
             | (b[:, 1].astype(np.int32) << 8)
             | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float32) / 8388608.0
    elif sw == 2:
        v = raw.view(np.int16).astype(np.float32) / 32768.0
    else:
        v = raw.astype(np.float32) / 127.0 - 1.0

    v = v.reshape(-1, ch)
    if ch == 1:
        v = np.repeat(v, 2, axis=1)

    if len(v) < n:
        v = np.pad(v, ((0, n - len(v)), (0, 0)))
    return v[:n]


def env_ramp(n, points):
    times = np.array([p[0] for p in points], dtype=np.float64)
    gains = np.array([p[1] for p in points], dtype=np.float64)
    t = np.arange(n) / SAMPLE_RATE
    return np.interp(t, times, gains).astype(np.float32)


def build_audio():
    n = int(DURATION * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    rng = np.random.default_rng(808909)

    stem_path = os.path.join(ASSETS, "replicant_tr8s_hardtechno_demo.wav")
    if not os.path.exists(stem_path):
        raise SystemExit(f"Missing {stem_path}; run ./build/generate_snapshot first.")

    drums = load_wav(stem_path, n)

    # Layer optional acid synth companion from Replicant303 if available, or synthesize dark bassline
    acid_path = "/PROCESOS/Replicant303/assets/replicant303_hardtechno_demo.wav"
    if os.path.exists(acid_path):
        acid = load_wav(acid_path, n) * 0.45
    else:
        acid = np.zeros((n, 2), dtype=np.float32)

    # Cinematic A-minor dark Vangelis/Blade-Runner pad
    pad = np.zeros((n, 2), dtype=np.float32)
    for ch, detune in ((0, 0.998), (1, 1.002)):
        p = np.zeros(n, dtype=np.float32)
        for f in (55.0, 82.407, 110.0, 130.813, 164.814):
            p += (2.0 * ((f * detune * t) % 1.0) - 1.0).astype(np.float32) * 0.15
        sos = signal.butter(2, 420.0, "lp", fs=SAMPLE_RATE, output="sos")
        pad[:, ch] = signal.sosfilt(sos, p).astype(np.float32)

    pad_gain = env_ramp(n, [(0, 0.60), (6, 0.32), (14, 0.18), (40, 0.20),
                            (56, 0.34), (62, 0.38), (64, 0.0)])

    # Rain / cyber atmosphere bed
    rain = np.zeros((n, 2), dtype=np.float32)
    for ch in range(2):
        noise = rng.standard_normal(n).astype(np.float32)
        sos = signal.butter(2, 1800.0, "hp", fs=SAMPLE_RATE, output="sos")
        noise = signal.sosfilt(sos, noise).astype(np.float32)
        slow = 0.7 + 0.3 * np.sin(2 * np.pi * t / 17.0 + ch)
        rain[:, ch] = noise * slow * 0.032

    drum_gain = env_ramp(n, [(0, 0.0), (2.5, 0.35), (6.0, 0.95), (57.0, 0.95),
                             (61.0, 0.40), (64.0, 0.0)])
    acid_gain = env_ramp(n, [(0, 0.0), (6.0, 0.25), (14.0, 0.75), (56.0, 0.75),
                             (60.0, 0.25), (64.0, 0.0)])

    mix = (drums * drum_gain[:, None] * 0.95
           + acid * acid_gain[:, None]
           + pad * pad_gain[:, None] * 0.78
           + rain * env_ramp(n, [(0, 1.0), (10, 0.25), (54, 0.25), (60, 0.7), (64, 0.0)])[:, None])

    mix = np.tanh(mix * 1.12) / np.tanh(1.12)
    peak = np.abs(mix).max()
    if peak > 0:
        mix *= 0.82 / peak

    fade = int(0.6 * SAMPLE_RATE)
    mix[:fade] *= np.linspace(0.0, 1.0, fade)[:, None]
    mix[-int(2.0 * SAMPLE_RATE):] *= np.linspace(1.0, 0.0, int(2.0 * SAMPLE_RATE))[:, None]

    out = os.path.join(ASSETS, "replicant_tr8s_hardtechno_mix.wav")
    pcm = (np.clip(mix, -1.0, 1.0) * 32767.0).astype(np.int16)
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.tobytes())

    print(f"[audio] wrote {out} ({DURATION:.0f}s, peak {np.abs(mix).max():.2f})")
    return out


def spaced_text(draw, xy, text, fnt, fill, spacing=0.0, anchor="la"):
    widths = [draw.textlength(c, font=fnt) for c in text]
    total = sum(widths) + spacing * max(len(text) - 1, 0)
    x, y = xy
    if anchor[0] == "m":
        x -= total / 2.0
    elif anchor[0] == "r":
        x -= total
    for c, w in zip(text, widths):
        draw.text((x, y), c, font=fnt, fill=fill)
        x += w + spacing
    return total


def spaced_width(draw, text, fnt, spacing=0.0):
    widths = [draw.textlength(c, font=fnt) for c in text]
    return sum(widths) + spacing * max(len(text) - 1, 0)


def make_city():
    rng = np.random.default_rng(808)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sky = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sky)
    for i in range(26):
        y = 700 + i * 9
        col = (0, 210, 110, max(0, 24 - i))
        sd.rectangle([0, y, W, y + 9], fill=col)
    layer = Image.alpha_composite(layer, sky.filter(ImageFilter.GaussianBlur(28)))
    d = ImageDraw.Draw(layer)

    x = -40
    while x < W + 40:
        bw = int(rng.integers(50, 150))
        bh = int(rng.integers(160, 420))
        top = H - 180 - bh
        shade = int(rng.integers(9, 18))
        d.rectangle([x, top, x + bw, H], fill=(shade, shade + 5, shade + 9, 255))
        x += bw + int(rng.integers(6, 24))

    x = -80
    while x < W + 80:
        bw = int(rng.integers(90, 240))
        bh = int(rng.integers(220, 560))
        top = H - 40 - bh
        shade = int(rng.integers(4, 11))
        d.rectangle([x, top, x + bw, H], fill=(shade, shade + 3, shade + 6, 255))
        cols = max(2, bw // 22)
        rows = max(3, bh // 26)
        for c in range(cols):
            for r in range(rows):
                if rng.random() < 0.14:
                    wx = x + 8 + c * 22
                    wy = top + 14 + r * 26
                    if wx + 4 < x + bw and wy + 6 < H:
                        col = GREEN if rng.random() < 0.45 else (AMBER if rng.random() < 0.6 else CYAN)
                        a = int(rng.integers(25, 110))
                        d.rectangle([wx, wy, wx + 5, wy + 7], fill=col + (a,))
        x += bw + int(rng.integers(4, 18))

    return layer


def make_fog():
    rng = np.random.default_rng(19)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(18):
        x = int(rng.integers(-200, W + 200))
        y = int(rng.integers(520, 1000))
        rx = int(rng.integers(180, 520))
        ry = int(rng.integers(40, 130))
        col = GREEN + (int(rng.integers(5, 15)),) if rng.random() < 0.5 else (80, 160, 185, int(rng.integers(5, 14)))
        d.ellipse([x - rx, y - ry, x + rx, y + ry], fill=col)
    return layer.filter(ImageFilter.GaussianBlur(70))


def make_vignette():
    y, x = np.ogrid[:H, :W]
    cx, cy = W / 2, H / 2
    r = np.sqrt(((x - cx) / (W * 0.62)) ** 2 + ((y - cy) / (H * 0.62)) ** 2)
    alpha = np.clip((r - 0.55) * 220, 0, 190).astype(np.uint8)
    arr = np.zeros((H, W, 4), dtype=np.uint8)
    arr[..., 3] = alpha
    return Image.fromarray(arr, "RGBA")


def make_scanlines():
    arr = np.zeros((H, W, 4), dtype=np.uint8)
    arr[::3, :, 3] = 34
    arr[1::3, :, 3] = 12
    return Image.fromarray(arr, "RGBA")


def make_noise_tiles(count=5):
    rng = np.random.default_rng(23)
    tiles = []
    for _ in range(count):
        small = rng.integers(0, 28, size=(H // 4, W // 4), dtype=np.uint8)
        arr = np.zeros((H, W, 4), dtype=np.uint8)
        arr[..., 3] = np.repeat(np.repeat(small, 4, axis=0), 4, axis=1)
        tiles.append(Image.fromarray(arr, "RGBA"))
    return tiles


def gradient_background(top=(7, 12, 18), bottom=(1, 2, 4)):
    img = Image.new("RGB", (W, H), bottom)
    px = img.load()
    for y in range(H):
        k = y / (H - 1)
        c = tuple(int(top[i] * (1 - k) + bottom[i] * k) for i in range(3))
        for x in range(W):
            px[x, y] = c
    return img


def draw_rain(img, t, intensity=1.0):
    rng = np.random.default_rng(int(t * 1000) + 5)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    count = int(260 * intensity)
    for _ in range(count):
        x = int(rng.integers(-100, W + 100))
        y = int(rng.integers(0, H))
        length = int(rng.integers(18, 70))
        dx = int(length * 0.16)
        a = int(rng.integers(14, 55))
        col = (170, 200, 215, a) if rng.random() < 0.8 else GREEN + (a,)
        d.line([(x, y), (x + dx, y + length)], fill=col, width=1)
    img.alpha_composite(layer)


def draw_ui(frame, src, rect, accent=GREEN, scan_t=None):
    x, y, w, h = rect
    shot = src.resize((w, h), Image.LANCZOS)

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.rectangle([x - 14, y - 14, x + w + 14, y + h + 14], outline=accent + (90,), width=10)
    frame.alpha_composite(glow.filter(ImageFilter.GaussianBlur(22)))

    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rectangle([x + 10, y + 16, x + w + 10, y + h + 16], fill=(0, 0, 0, 170))
    frame.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))

    frame.alpha_composite(shot.convert("RGBA"), (x, y))

    d = ImageDraw.Draw(frame)
    d.rectangle([x - 2, y - 2, x + w + 2, y + h + 2], outline=accent + (210,), width=2)

    tick = 26
    for cx, cy, sx, sy in ((x - 8, y - 8, 1, 1), (x + w + 8, y - 8, -1, 1),
                           (x - 8, y + h + 8, 1, -1), (x + w + 8, y + h + 8, -1, -1)):
        d.line([(cx, cy), (cx + sx * tick, cy)], fill=CYAN + (200,), width=2)
        d.line([(cx, cy), (cx, cy + sy * tick)], fill=CYAN + (200,), width=2)

    if scan_t is not None:
        sy = y + int((scan_t % 1.0) * h)
        sl = Image.new("RGBA", (W, 40), (0, 0, 0, 0))
        sld = ImageDraw.Draw(sl)
        sld.rectangle([x, 18, x + w, 22], fill=accent + (65,))
        frame.alpha_composite(sl.filter(ImageFilter.GaussianBlur(6)), (0, sy - 20))


def caption(frame, x, y, kicker, title, sub=None, colour=GREEN):
    d = ImageDraw.Draw(frame)
    d.line([(x, y - 16), (x + 70, y - 16)], fill=colour + (255,), width=3)
    d.text((x, y), kicker, font=font(FONT_MONO, 22), fill=colour + (255,))
    d.text((x, y + 34), title, font=font(FONT_TITLE, 56), fill=WHITE + (255,))
    if sub:
        d.text((x, y + 100), sub, font=font(FONT_MONO_REG, 22), fill=MUTED + (255,))


def big_title(frame, text, y, size=180, colour=GREEN, spacing=6.0, alpha=255, glitch=0.0):
    d0 = ImageDraw.Draw(frame)
    fnt = font(FONT_TITLE, size)
    total = spaced_width(d0, text, fnt, spacing)
    x = (W - total) / 2.0

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    spaced_text(gd, (x, y), text, fnt, colour + (255,), spacing)
    frame.alpha_composite(glow.filter(ImageFilter.GaussianBlur(26)))

    layer_r = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dr = ImageDraw.Draw(layer_r)
    spaced_text(dr, (x - 3 - glitch, y), text, fnt, (255, 30, 60, 120), spacing)
    frame.alpha_composite(layer_r)

    layer_c = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dc = ImageDraw.Draw(layer_c)
    spaced_text(dc, (x + 3 + glitch, y), text, fnt, (40, 220, 255, 110), spacing)
    frame.alpha_composite(layer_c)

    d = ImageDraw.Draw(frame)
    spaced_text(d, (x, y), text, fnt, colour + (alpha,), spacing)


def kick_pulse(t):
    beat = 60.0 / 138.0
    if t < 6.0:
        return 0.0
    phase = ((t - 6.0) % beat) / beat
    return float(max(0.0, 1.0 - phase * 3.2))


def scene_intro(frame, t):
    d = ImageDraw.Draw(frame)
    d.text((120, 96), "OFF-WORLD AUDIO DIVISION // RHYTHM 8S", font=font(FONT_MONO, 24), fill=CYAN + (220,))
    d.text((W - 300, 96), "LOS ANGELES  //  2049", font=font(FONT_MONO, 24), fill=MUTED + (180,))

    lines = [
        (0.5, "> BOOTING REPLICANT TR-8S CORE ............. OK"),
        (1.4, "> 11 CHANNELS  ·  ACB + FM + USER SAMPLER .. READY"),
        (2.3, "> PER-CHANNEL REVERB & DELAY SENDS ......... ENGAGED"),
        (3.2, "> MIXER CEILING CAPPED AT 150 (ANTI-CLIP) .. LOCKED"),
        (4.1, "> ALL FADERS INITIALIZED AT MAX 150 ........ ONLINE"),
        (4.9, "> SIGNAL ACQUIRED"),
    ]

    y = 320
    for at, line in lines:
        if t < at:
            break
        shown = line[: max(0, int((t - at) * 44) + 1)]
        d.text((150, y), shown, font=font(FONT_MONO_REG, 27), fill=GREEN + (235,))
        y += 50

    if int(t * 2.5) % 2 == 0:
        d.rectangle([150, y + 2, 168, y + 30], fill=GREEN + (220,))

    width = int(min(1.0, t / 5.0) * 680)
    d.line([(150, 285), (150 + width, 285)], fill=GREEN + (190,), width=2)


def scene_title(frame, t, local):
    big_title(frame, "REPLICANT TR-8S", 360, 172, GREEN, 7.0, glitch=4.0 if local < 0.35 else 0.0)

    d = ImageDraw.Draw(frame)
    d.line([(620, 640), (1300, 640)], fill=GREEN + (200,), width=2)
    spaced_text(d, (W / 2, 680), "HARD TECHNO // RHYTHM PERFORMER",
                font(FONT_MONO, 30), CYAN + (255,), 3.0, anchor="ma")
    d.text((W / 2, 755), "PER-CHANNEL REVERB & DELAY   ·   150 MAX ANTI-CLIP MIXER",
           font=font(FONT_MONO_REG, 22), fill=AMBER + (240,), anchor="ma")
    d.text((W / 2, 815), "ACB 808 / 909 / 707 / 727 / 606 / CR-78   ·   FM   ·   VST3 / AU / STANDALONE",
           font=font(FONT_MONO_REG, 19), fill=MUTED + (185,), anchor="ma")


def scene_panel(frame, t, local):
    draw_ui(frame, PANEL_909, (180, 168, 1560, 888), GREEN, scan_t=local * 0.55)
    caption(frame, 180, 24, "01 / THE RHYTHM PERFORMER", "11-CHANNEL HARDWARE CONSOLE",
            "ACB + FM + SAMPLE ENGINE  ·  FIRMWARE 3.00 ARCHITECTURE  ·  16 RGB STEP PADS")


def scene_reverb_delay_zoom(frame, t, local):
    # Crop channel strips section showing TUNE -> DECAY -> REVERB / DELAY -> CTRL
    pw, ph = PANEL_909.size
    crop = PANEL_909.crop((int(pw * 0.012), int(ph * 0.275), int(pw * 0.65), int(ph * 0.61)))
    rect = (120, 210, 1680, 680)
    draw_ui(frame, crop, rect, CYAN, scan_t=local * 0.85)

    # Highlight the REVERB & DELAY row between DECAY and CTRL with animated neon brackets
    d = ImageDraw.Draw(frame)
    ry1 = rect[1] + int(rect[3] * 0.45)
    ry2 = rect[1] + int(rect[3] * 0.68)
    pulse_a = int(170 + 75 * math.sin(t * 6.0))
    d.rectangle([rect[0] + 8, ry1, rect[0] + rect[2] - 8, ry2], outline=CYAN + (pulse_a,), width=3)

    caption(frame, 120, 42, "02 / NEW PER-CHANNEL FX SENDS",
            "REVERB & DELAY BETWEEN DECAY AND CTRL",
            "DEDICATED REVERB AND DELAY KNOBS ON EVERY CHANNEL STRIP (BD TO RC)", colour=CYAN)


def scene_mixer_150_zoom(frame, t, local):
    # Crop the mixer faders, channel headers (LVL 150) and LCD LEVEL 150 readout
    pw, ph = PANEL_808.size
    crop = PANEL_808.crop((int(pw * 0.012), int(ph * 0.275), int(pw * 0.988), int(ph * 0.775)))
    rect = (120, 215, 1680, 710)
    draw_ui(frame, crop, rect, AMBER, scan_t=local * 0.75)

    d = ImageDraw.Draw(frame)
    # Highlight top header LVL 150 and fader bank
    fy1 = rect[1] + int(rect[3] * 0.58)
    fy2 = rect[1] + int(rect[3] * 0.88)
    pulse_a = int(170 + 75 * math.sin(t * 6.0))
    d.rectangle([rect[0] + 8, fy1, rect[0] + rect[2] - 8, fy2], outline=AMBER + (pulse_a,), width=3)

    caption(frame, 120, 42, "03 / ANTI-SATURATION MIXER",
            "CAPPED AT 150  ·  STARTS AT MAX LEVEL",
            "ALL 11 CHANNEL FADERS INITIALIZE AT 150/150 FOR MAXIMUM PUNCH WITHOUT CLIPPING", colour=AMBER)


def scene_sequencer(frame, t, local):
    pw, ph = PANEL_909.size
    crop = PANEL_909.crop((0, int(ph * 0.52), pw, ph))
    rect = (130, 250, 1660, 680)
    draw_ui(frame, crop, rect, GREEN, scan_t=local * 0.9)

    # Animated 16-step playhead sweep
    step = int((t / (60.0 / 138.0 / 4.0))) % 16
    pad_start_x = rect[0] + int(rect[2] * (18.0 / 1280.0))
    pad_step_w = (rect[2] * (1244.0 / 1280.0)) / 16.0
    px = int(pad_start_x + step * pad_step_w)
    py = rect[1] + int(rect[3] * 0.69)
    pw_box = int(pad_step_w - 6)
    ph_box = int(rect[3] * 0.26)

    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.rectangle([px, py, px + pw_box, py + ph_box], fill=GREEN + (75,), outline=WHITE + (240,), width=3)
    frame.alpha_composite(layer)

    caption(frame, 130, 60, "04 / TR-REC STEP SEQUENCER",
            "16 RGB PADS  ·  8 VARIATIONS A-H",
            "ACCENT TRACK  ·  FLAM SUBSTEPS  ·  AUTO FILL-IN  ·  DAW HOST TRANSPORT SYNC", colour=GREEN)


def scene_fm_scatter(frame, t, local):
    draw_ui(frame, PANEL_FM, (180, 168, 1560, 888), RED, scan_t=local * 0.65)
    caption(frame, 180, 24, "05 / FM SYNTHESIS & AIRA SCATTER",
            "FM DRUM MORPH  ·  8 SCATTER MODES",
            "REVERSE  ·  GATE  ·  REPEAT  ·  HALF-SPEED  ·  CHOP  ·  BIT-CRUSH  ·  PAN", colour=RED)


def scene_feature(frame, t, local, index, title, sub, sub2):
    d = ImageDraw.Draw(frame)
    d.text((150, 230), f"0{index} / 03  //  KEY UPGRADE", font=font(FONT_MONO, 26), fill=CYAN + (220,))
    d.line([(150, 290), (150 + 160, 290)], fill=GREEN + (255,), width=3)

    spaced_text(d, (150, 340), title, font(FONT_TITLE, 92), GREEN + (255,), 2.0)
    d.text((150, 480), sub, font=font(FONT_MONO, 30), fill=WHITE + (240,))
    d.text((150, 535), sub2, font=font(FONT_MONO_REG, 24), fill=AMBER + (225,))

    pts = []
    for i in range(0, 1620, 8):
        x = 150 + i
        y = 800 + math.sin(i * 0.02 + t * 3.5) * 36 * math.sin(i * 0.004 + t)
        pts.append((x, y))
    if len(pts) > 1:
        d.line(pts, fill=GREEN + (80,), width=2)


def scene_end(frame, t, local):
    d = ImageDraw.Draw(frame)

    cx, cy = W / 2, 290
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - 62, cy - 62, cx + 62, cy + 62], outline=GREEN + (255,), width=3)
    wave_pts = [(cx - 42 + i * 2, cy + 16 * math.sin(i * 0.25)) for i in range(43)]
    gd.line(wave_pts, fill=GREEN + (255,), width=5)
    frame.alpha_composite(glow.filter(ImageFilter.GaussianBlur(12)))
    d.ellipse([cx - 62, cy - 62, cx + 62, cy + 62], outline=GREEN + (230,), width=3)
    d.line(wave_pts, fill=GREEN + (255,), width=5)

    big_title(frame, "REPLICANT TR-8S", 390, 145, GREEN, 7.0)

    d.line([(680, 600), (1240, 600)], fill=GREEN + (180,), width=2)
    spaced_text(d, (W / 2, 640), "PER-CHANNEL REVERB & DELAY  ·  150 MAX MIXER",
                font(FONT_MONO, 25), CYAN + (240,), 2.0, anchor="ma")
    d.text((W / 2, 710), "VST3   ·   AU   ·   STANDALONE",
           font=font(FONT_MONO_REG, 22), fill=MUTED + (220,), anchor="ma")
    d.text((W / 2, 780), "github.com/DanimanX/ReplicantTr8s",
           font=font(FONT_MONO_REG, 24), fill=WHITE + (230,), anchor="ma")
    d.text((W / 2, 835), "DANIMANX", font=font(FONT_MONO, 20), fill=AMBER + (190,), anchor="ma")


CITY = None
FOG = None
VIGNETTE = None
SCAN = None
NOISE = None
BG = None
PANEL_909 = None
PANEL_808 = None
PANEL_FM = None


def init_visuals():
    global CITY, FOG, VIGNETTE, SCAN, NOISE, BG, PANEL_909, PANEL_808, PANEL_FM
    CITY = make_city()
    FOG = make_fog()
    VIGNETTE = make_vignette()
    SCAN = make_scanlines()
    NOISE = make_noise_tiles()
    BG = gradient_background()
    PANEL_909 = Image.open(os.path.join(ASSETS, "replicant_tr8s_preview.png")).convert("RGB")
    PANEL_808 = Image.open(os.path.join(ASSETS, "replicant_tr8s_808.png")).convert("RGB")
    PANEL_FM = Image.open(os.path.join(ASSETS, "replicant_tr8s_fm.png")).convert("RGB")


def render_frame(t):
    frame = BG.convert("RGBA").copy()
    frame.alpha_composite(CITY)
    frame.alpha_composite(FOG, (int(30 * math.sin(t * 0.13)), int(12 * math.sin(t * 0.21))))
    draw_rain(frame, t, 1.0 if t < 8 or t > 52 else 0.55)

    pulse = kick_pulse(t)
    if pulse > 0.01:
        glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow)
        gd.ellipse([W / 2 - 520, 120, W / 2 + 520, 700], fill=(0, 220, 100, int(10 + 26 * pulse)))
        frame.alpha_composite(glow.filter(ImageFilter.GaussianBlur(80)))

    if t < 6.0:
        scene_intro(frame, t)
    elif t < 14.0:
        scene_title(frame, t, t - 6.0)
    elif t < 23.0:
        scene_panel(frame, t, t - 14.0)
    elif t < 32.0:
        scene_reverb_delay_zoom(frame, t, t - 23.0)
    elif t < 41.0:
        scene_mixer_150_zoom(frame, t, t - 32.0)
    elif t < 47.0:
        scene_sequencer(frame, t, t - 41.0)
    elif t < 51.0:
        scene_fm_scatter(frame, t, t - 47.0)
    elif t < 53.5:
        scene_feature(frame, t, t - 51.0, 1, "PER-CHANNEL REVERB & DELAY",
                      "POSITIONED BETWEEN DECAY AND CTRL ON ALL 11 CHANNELS",
                      "LUSH STEREO HALL REVERB  ·  TEMPO-SYNCED PING-PONG DELAY")
    elif t < 56.0:
        scene_feature(frame, t, t - 53.5, 2, "MAX 150 ANTI-CLIP MIXER",
                      "NEVER EXCEEDS 150  ·  ALWAYS STARTS AT MAXIMUM LEVEL 150",
                      "ENGINEERED FOR CLEAN HEADROOM WITH ZERO BUS SATURATION")
    elif t < 58.0:
        scene_feature(frame, t, t - 56.0, 3, "ACB + FM + USER SAMPLER",
                      "808  ·  909  ·  707  ·  727  ·  606  ·  CR-78  ·  FM MORPH",
                      "DRAG & DROP WAV / AIFF / FLAC / MP3 ONTO ANY CHANNEL STRIP")
    else:
        scene_end(frame, t, t - 58.0)

    frame.alpha_composite(SCAN)
    frame.alpha_composite(VIGNETTE)

    if t > 0.4:
        frame.alpha_composite(NOISE[int(t * 30) % len(NOISE)])

    if t < 0.5:
        k = t / 0.5
        frame = Image.eval(frame, lambda v: int(v * k))
    if t > DURATION - 2.0:
        k = max(0.0, (DURATION - t) / 2.0)
        frame = Image.eval(frame, lambda v: int(v * k))

    cuts = (6.0, 14.0, 23.0, 32.0, 41.0, 47.0, 51.0, 53.5, 56.0, 58.0)
    for cut in cuts:
        if 0 <= t - cut < 0.12:
            arr = np.array(frame)
            rng = np.random.default_rng(int(t * 10000) + int(cut))
            for _ in range(5):
                y0 = int(rng.integers(0, H - 60))
                hgt = int(rng.integers(10, 60))
                arr[y0:y0 + hgt] = np.roll(arr[y0:y0 + hgt], int(rng.integers(-40, 40)), axis=1)
            frame = Image.fromarray(arr)
            break

    return frame.convert("RGB")


def main():
    mix_path = build_audio()
    init_visuals()

    out_path = os.path.join(ASSETS, "replicant_tr8s_promo_hardtechno_1080p.mp4")
    poster_path = os.path.join(ASSETS, "replicant_tr8s_promo_hardtechno_poster.png")

    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
        "-i", mix_path,
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        "-c:a", "aac", "-b:a", "192k",
        "-shortest", out_path,
    ]

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    total = int(DURATION * FPS)

    for i in range(total):
        t = i / FPS
        frame = render_frame(t)
        if i == int(25.0 * FPS):
            frame.save(poster_path)
        proc.stdin.write(frame.tobytes())
        if i % 150 == 0:
            print(f"[video] frame {i}/{total} ({t:5.1f}s)")

    proc.stdin.close()
    rc = proc.wait()
    if rc != 0:
        raise SystemExit(f"ffmpeg failed with exit code {rc}")
    print(f"[video] wrote {out_path} and {poster_path}")


if __name__ == "__main__":
    main()
