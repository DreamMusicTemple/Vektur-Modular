# 3. Standalone app and AUv3 plug-in

**Standalone.** The app opens its own audio session, listens to every MIDI source (USB, Bluetooth, network and other apps) and offers a virtual MIDI source called *Vektur Modular*. Your session is saved when the app goes to the background and restored the next time it opens.

**AUv3.** Add *DMT Cymatics: Vektur Modular* as an instrument in your host. The host saves the complete state with the project: parameters, patch, clips, effects, MIDI map and appearance. All parameters can be automated and are listed in chapter 31. The plug-in reports its latency and tail time, so hosts can compensate.

Some settings belong only to the standalone app and are hidden in a plug-in: Keep Screen Awake, Background Audio and the audio buffer size. In a host, the host controls those.
