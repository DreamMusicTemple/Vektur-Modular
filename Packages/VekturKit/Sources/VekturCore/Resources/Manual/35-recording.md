# 35. Recording

The recorder on the Mixer screen captures the instrument's output, including effects, to a file. You can choose:

- **Format:** WAV, AIFF or CAF.
- **Bit depth:** 16-bit, 24-bit or 32-bit float.
- **Sample rate:** the session rate, or converted to 44.1, 48, 88.2 or 96 kHz.

Recordings are saved in *Recordings* in the app's Documents folder, which you can see in the Files app on iPhone and iPad. Share them with the share button.

The audio thread only copies samples into an 11-second buffer, and a background task writes the file. This means recording never causes dropouts. If the device ever falls that far behind, the recorder counts the frames it had to skip and shows the count, rather than interrupting the audio.

Keyboard shortcut: ⇧⌘R starts and stops recording.
