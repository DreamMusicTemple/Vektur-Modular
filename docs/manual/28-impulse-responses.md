# 28. Impulse responses

The convolution reverb loads your own impulse response files (WAV, AIFF or CAF, up to 10 seconds). Files are copied into the app's shared library and referred to by content hash, so a project opens with the right IR on any of your devices that has the file. If the file is missing, the slot stays silent and says so. It never loads a different file instead.
