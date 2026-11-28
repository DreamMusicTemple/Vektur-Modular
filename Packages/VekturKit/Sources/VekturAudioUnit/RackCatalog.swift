// RackCatalog.swift — Swift view of the C++ rack (modules, ports, normalled
// connections), read once through the C introspection API so the UI and the
// engine can never disagree about what exists.
import VekturCore
import VekturDSP

public enum PortKind: UInt8, Sendable {
    case audio = 0, cv = 1, gate = 2, trigger = 3
    public var label: String { ["Audio", "CV", "Gate", "Trigger"][Int(rawValue)] }
}

public struct RackPort: Sendable, Hashable, Identifiable {
    public let index: Int
    public let key: String
    public let name: String
    public let kind: PortKind
    public let isInput: Bool
    /// Parameter whose value scales this input (the knob beside the jack), if any.
    public let attenuatorAddress: UInt64?
    public var id: String { "\(isInput ? "in" : "out").\(key)" }
}

public struct RackModule: Sendable, Identifiable {
    public let index: Int
    public let key: String
    public let name: String
    /// Parameter address block (address = block × 256 + index), nil for parameterless modules.
    public let paramModule: Int?
    public let inputs: [RackPort]
    public let outputs: [RackPort]
    public var id: String { key }
    public var isPatchable: Bool { !inputs.isEmpty || !outputs.isEmpty }
}

public struct RackNormal: Sendable, Hashable {
    public let index: Int
    public let from: PortRef
    public let to: PortRef
    /// Telemetry slot for vk_kernel_connection_level.
    public var telemetrySlot: UInt32 { VK_NORMAL_SLOT_BASE + UInt32(index) }
}

public enum RackCatalog {
    public static let modules: [RackModule] = loadModules()
    public static let normals: [RackNormal] = loadNormals()

    public static var patchableModules: [RackModule] { modules.filter(\.isPatchable) }

    public static func module(key: String) -> RackModule? { modules.first { $0.key == key } }

    /// Resolves a saved port reference to engine indices.
    public static func resolve(_ ref: PortRef, input: Bool) -> (module: Int, port: Int)? {
        guard let m = module(key: ref.module) else { return nil }
        let ports = input ? m.inputs : m.outputs
        guard let p = ports.first(where: { $0.key == ref.port }) else { return nil }
        return (m.index, p.index)
    }

    public static func displayName(_ ref: PortRef, input: Bool) -> String {
        guard let m = module(key: ref.module) else { return ref.description }
        let ports = input ? m.inputs : m.outputs
        return "\(m.name) · \(ports.first { $0.key == ref.port }?.name ?? ref.port)"
    }

    private static func loadModules() -> [RackModule] {
        (0..<Int(vk_rack_module_count())).compactMap { i -> RackModule? in
            var info = VKModuleInfo()
            guard vk_rack_module_info(UInt32(i), &info) else { return nil }
            func ports(_ input: Bool, _ count: UInt8) -> [RackPort] {
                (0..<Int(count)).compactMap { p in
                    var pi = VKPortInfo()
                    guard vk_rack_port_info(UInt32(i), UInt32(p), input, &pi) else { return nil }
                    return RackPort(index: p, key: String(cString: pi.key), name: String(cString: pi.name),
                                    kind: PortKind(rawValue: pi.kind) ?? .audio, isInput: input,
                                    attenuatorAddress: pi.attenuatorAddress >= 0 ? UInt64(pi.attenuatorAddress) : nil)
                }
            }
            return RackModule(index: i, key: String(cString: info.key), name: String(cString: info.name),
                              paramModule: info.paramModule >= 0 ? Int(info.paramModule) : nil,
                              inputs: ports(true, info.inputCount), outputs: ports(false, info.outputCount))
        }
    }

    private static func loadNormals() -> [RackNormal] {
        // Hidden routing modules report no ports, so read their keys directly.
        func portKey(_ module: UInt8, _ port: UInt8, _ input: Bool) -> PortRef {
            var mi = VKModuleInfo(); _ = vk_rack_module_info(UInt32(module), &mi)
            var pi = VKPortInfo()
            let ok = vk_rack_port_info(UInt32(module), UInt32(port), input, &pi)
            return PortRef(module: String(cString: mi.key), port: ok ? String(cString: pi.key) : "\(port)")
        }
        return (0..<Int(vk_rack_normal_count())).compactMap { i in
            var n = VKNormalInfo()
            guard vk_rack_normal_info(UInt32(i), &n) else { return nil }
            return RackNormal(index: i, from: portKey(n.srcModule, n.srcPort, false), to: portKey(n.dstModule, n.dstPort, true))
        }
    }
}
