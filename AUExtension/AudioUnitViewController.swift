// AudioUnitViewController.swift — AUv3 extension principal class: the factory
// the host calls to create the audio unit, plus the plug-in's view.
import CoreAudioKit
import SwiftUI
import os
import VekturAudioUnit
import VekturUI

#if os(iOS)
import UIKit
typealias HostingController = UIHostingController<AnyView>
#else
import AppKit
typealias HostingController = NSHostingController<AnyView>
#endif

public final class AudioUnitViewController: AUViewController, AUAudioUnitFactory {

    /// Written by createAudioUnit (host thread), read on main.
    private let unitBox = OSAllocatedUnfairLock<VekturModularAudioUnit?>(uncheckedState: nil)
    private var hosting: HostingController?

    // Hosts may call this on any thread, before or after the view loads.
    nonisolated public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try VekturModularAudioUnit(componentDescription: componentDescription, options: [])
        unitBox.withLockUnchecked { $0 = au }
        Task { @MainActor in self.installUIIfReady() }
        return au
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        #if os(iOS)
        preferredContentSize = CGSize(width: 1024, height: 640)
        #else
        preferredContentSize = NSSize(width: 1024, height: 640)
        #endif
        installUIIfReady()
    }

    private func installUIIfReady() {
        guard isViewLoaded, hosting == nil, let au = unitBox.withLockUnchecked({ $0 }) else { return }
        let root = AnyView(MainView(audioUnit: au))
        let hc = HostingController(rootView: root)
        addChild(hc)
        hc.view.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(hc.view)
        NSLayoutConstraint.activate([
            hc.view.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            hc.view.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            hc.view.topAnchor.constraint(equalTo: view.topAnchor),
            hc.view.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        #if os(iOS)
        hc.didMove(toParent: self)
        #endif
        hosting = hc
    }
}
