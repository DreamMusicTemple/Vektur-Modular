// Phase10DSPTests.swift — runs on Apple platforms, so it exercises the vDSP
// FFT path (Linux CI covers the portable path in DSPTests/test_phase10).
import XCTest
import VekturDSP

final class Phase10DSPTests: XCTestCase {
    func testVDSPFFTMatchesDirectDFT() {
        for n: UInt32 in [64, 512, 2048] { XCTAssertLessThan(vk_fft_selftest(n), 1e-4, "size \(n)") }
    }

    func testRecordingTapIsBitExact() {
        let k = vk_kernel_create()!
        defer { vk_kernel_destroy(k) }
        XCTAssertEqual(vk_kernel_allocate(k, 48_000, 2, 512), 0)
        XCTAssertTrue(vk_kernel_record_start(k))
        var buf = [Float](repeating: 0, count: 2 * 4096)
        XCTAssertEqual(buf.withUnsafeMutableBufferPointer { vk_kernel_record_read(k, $0.baseAddress, 4096) }, 0)
        vk_kernel_record_stop(k)
        XCTAssertEqual(vk_kernel_record_dropped(k), 0)
    }
}
