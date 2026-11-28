// Verifies the Swift ↔ C boundary (constants, structs, render) on Apple platforms.
// The exhaustive DSP suite is DSPTests/test_kernel.cpp (runs without Xcode).
import XCTest
import VekturCore
import VekturDSP

final class KernelBridgeTests: XCTestCase {

    private func makeKernel() -> OpaquePointer {
        let k = vk_kernel_create()!
        XCTAssertEqual(vk_kernel_allocate(k, 48_000, 2, 512), 0)
        return k
    }

    func testNoteProducesSoundAndReleaseReturnsToSilence() {
        let k = makeKernel(); defer { vk_kernel_destroy(k) }
        let frames = 512
        let l = UnsafeMutablePointer<Float>.allocate(capacity: frames); defer { l.deallocate() }
        let r = UnsafeMutablePointer<Float>.allocate(capacity: frames); defer { r.deallocate() }
        var channels: [UnsafeMutablePointer<Float>?] = [l, r]

        var on = VKEvent(); on.type = UInt8(VK_EVENT_MIDI1.rawValue); on.length = 3; on.data = (0x90, 60, 100)
        var peak: Float = 0
        channels.withUnsafeMutableBufferPointer { ch in
            vk_kernel_render(k, ch.baseAddress, 2, UInt32(frames), &on, 1)
            for _ in 0..<20 { vk_kernel_render(k, ch.baseAddress, 2, UInt32(frames), nil, 0) }
        }
        for i in 0..<frames { peak = max(peak, abs(l[i])) }
        XCTAssertGreaterThan(peak, 0.05)

        var off = VKEvent(); off.type = UInt8(VK_EVENT_MIDI1.rawValue); off.length = 3; off.data = (0x80, 60, 0)
        channels.withUnsafeMutableBufferPointer { ch in
            vk_kernel_render(k, ch.baseAddress, 2, UInt32(frames), &off, 1)
            for _ in 0..<200 { vk_kernel_render(k, ch.baseAddress, 2, UInt32(frames), nil, 0) }
        }
        peak = 0
        for i in 0..<frames { peak = max(peak, abs(l[i])) }
        XCTAssertEqual(peak, 0)

        var t = VKTelemetry(); var got = false
        while vk_kernel_pop_telemetry(k, &t) { got = true }
        XCTAssertTrue(got)
    }

    /// Swift registry and C++ ParamTable must come from the same generator run.
    func testKernelDefaultsMatchRegistry() {
        let k = makeKernel(); defer { vk_kernel_destroy(k) }
        for s in ParameterRegistry.all {
            XCTAssertEqual(vk_kernel_get_param(k, s.address), s.defaultValue, s.identifier)
            vk_kernel_set_param(k, s.address, s.maxValue + 1000)
            XCTAssertEqual(vk_kernel_get_param(k, s.address), s.maxValue, "\(s.identifier) clamps to the same range")
        }
        XCTAssertEqual(ParameterRegistry.all.count, Int(VK_PARAM_COUNT))
    }

    func testParameterClampingThroughCAPI() {
        let k = makeKernel(); defer { vk_kernel_destroy(k) }
        let vol = ParameterRegistry.spec(identifier: ParameterRegistry.ID.masterVolume)!
        vk_kernel_set_param(k, vol.address, 50)
        XCTAssertEqual(vk_kernel_get_param(k, vol.address), vol.maxValue)
    }
}
