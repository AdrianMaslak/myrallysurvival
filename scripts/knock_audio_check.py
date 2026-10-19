"""Compares the pinking band (3.8-5.6 kHz) in the knock test's recording: the first 12 s at stock boost
against the next 12 s at 1.4 bar. Usage: python knock_audio_check.py RallyKnock_audio.wav"""
import sys
import wave

import numpy as np


def band_power(samples, rate, lo, hi):
    spectrum = np.abs(np.fft.rfft(samples * np.hanning(len(samples)))) ** 2
    freqs = np.fft.rfftfreq(len(samples), 1.0 / rate)
    return spectrum[(freqs >= lo) & (freqs <= hi)].sum()


def main(path):
    with wave.open(path, 'rb') as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    dtype = {2: np.int16, 4: np.int32}[width]
    data = np.frombuffer(raw, dtype=dtype).astype(np.float64).reshape(-1, channels).mean(axis=1)
    # Skip the first and last second of each half: the boost change and spool-up.
    clean = data[int(1 * rate):int(11 * rate)]
    knock = data[int(13 * rate):int(23 * rate)]
    if len(knock) < rate:
        print(f'recording too short: {len(data) / rate:.1f} s')
        return 1
    # The band's share of everything heard, so road noise and the listener's distance cancel out.
    def share(x):
        return band_power(x, rate, 3800, 5600) / max(band_power(x, rate, 50, 20000), 1e-12)
    ratio = share(knock) / max(share(clean), 1e-12)
    print(f'{len(data) / rate:.1f} s at {rate} Hz: pinking band {100 * share(clean):.1f}% of the sound at stock boost, '
          f'{100 * share(knock):.1f}% at 1.4 bar (x{ratio:.1f})')
    return 0 if ratio > 2.0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
