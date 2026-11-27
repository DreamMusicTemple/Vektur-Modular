// ParameterModel.swift — two-way binding between a SwiftUI control and an
// AUParameter. Host automation and preset loads flow back into the UI.
@preconcurrency import AudioToolbox
import Observation

@MainActor
@Observable
public final class ParameterModel {
    public let parameter: AUParameter
    public var value: Float {
        didSet { if value != oldValue && !applyingRemote { parameter.setValue(value, originator: token) } }
    }
    public var displayString: String { parameter.string(fromValue: [value]) }

    @ObservationIgnored private var token: AUParameterObserverToken?
    @ObservationIgnored private var applyingRemote = false
    /// Removes the observer when the model goes away (deinit-safe under Swift 6).
    @ObservationIgnored private var remover: ObserverRemover?

    public init(_ parameter: AUParameter) {
        self.parameter = parameter
        self.value = parameter.value
        token = parameter.token(byAddingParameterObserver: { [weak self] _, newValue in
            guard let model = self else { return }   // MainActor-isolated ⇒ Sendable
            Task { @MainActor in model.receive(newValue) }
        })
        if let token { remover = ObserverRemover(parameter: parameter, token: token) }
    }

    private func receive(_ v: Float) {
        applyingRemote = true
        value = v
        applyingRemote = false
    }
}

private final class ObserverRemover: @unchecked Sendable {
    let parameter: AUParameter
    let token: AUParameterObserverToken
    init(parameter: AUParameter, token: AUParameterObserverToken) { self.parameter = parameter; self.token = token }
    deinit { parameter.removeParameterObserver(token) }
}
