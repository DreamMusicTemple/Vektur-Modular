// KernelHandle.swift — owns the C++ kernel and exposes its thread-safe API.
import Foundation
import VekturCore
import VekturDSP

/// The kernel's public functions are lock-free and individually documented as
/// [any], [ui] or [control] (see VekturDSP.h), which is why this wrapper can be
/// shared across threads.
public final class KernelHandle: @unchecked Sendable {
    /// Raw pointer for the render block. Valid for the lifetime of this object.
    public let raw: OpaquePointer

    public init() {
        guard let k = vk_kernel_create() else { fatalError("Vektur DSP kernel allocation failed") }
        raw = k
        // The kernel registers its own parameters from the generated ParamTable.hpp.
        // Verify Swift and C++ were generated from the same table.
        #if DEBUG
        for spec in ParameterRegistry.all {
            assert(vk_kernel_get_param(raw, spec.address) == spec.defaultValue,
                   "Kernel/registry mismatch for \(spec.identifier) — re-run tools/params.py")
        }
        #endif
    }

    deinit { vk_kernel_destroy(raw) }

    // [control]
    public func allocate(sampleRate: Double, channels: UInt32, maxFrames: UInt32) -> Int32 {
        vk_kernel_allocate(raw, sampleRate, channels, maxFrames)
    }
    public func deallocate() { vk_kernel_deallocate(raw) }
    public var isAllocated: Bool { vk_kernel_is_allocated(raw) }

    // [any]
    public func set(_ address: UInt64, _ value: Float) { vk_kernel_set_param(raw, address, value) }
    public func get(_ address: UInt64) -> Float { vk_kernel_get_param(raw, address) }

    // [ui] — single producer / single consumer: call from the main thread only.
    @discardableResult
    public func post(_ type: VKCommandType, arg: UInt32 = 0, value: Float = 0) -> Bool {
        vk_kernel_post_command(raw, VKCommand(type: type.rawValue, arg0: arg, value: value))
    }

    public func popTelemetry() -> VKTelemetry? {
        var t = VKTelemetry()
        return vk_kernel_pop_telemetry(raw, &t) ? t : nil
    }
}
