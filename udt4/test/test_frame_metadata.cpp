/*
 * Test program for frame metadata functionality in CPacket
 * This program tests the frame_id, chunk_id, and total_chunks methods
 */

#include <iostream>
#include <cstring>
#include "../src/packet.h"

using namespace std;

// ANSI color codes for output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"
#define YELLOW "\033[33m"

bool test_basic_set_get() {
    cout << "\n[TEST 1] Basic Set/Get Frame Metadata\n";
    cout << "======================================\n";

    CPacket pkt;

    // Set values
    int32_t frame_id = 12345;
    int32_t chunk_id = 123;
    int32_t total_chunks = 200;
    int64_t frame_deadline = 1234567890;

    cout << "Setting: frame_id=" << frame_id
         << ", chunk_id=" << chunk_id
         << ", total_chunks=" << total_chunks
         << ", frame_deadline=" << frame_deadline << endl;

    pkt.setFrameID(frame_id);
    pkt.setChunkID(chunk_id);
    pkt.setTotalChunks(total_chunks);
    pkt.setFrameDeadline(frame_deadline);

    // Get values back
    int32_t got_frame_id = pkt.getFrameID();
    int32_t got_chunk_id = pkt.getChunkID();
    int32_t got_total_chunks = pkt.getTotalChunks();
    int64_t got_frame_deadline = pkt.getFrameDeadline();

    cout << "Retrieved: frame_id=" << got_frame_id
         << ", chunk_id=" << got_chunk_id
         << ", total_chunks=" << got_total_chunks
         << ", frame_deadline=" << got_frame_deadline << endl;

    // Verify
    bool passed = (got_frame_id == frame_id) &&
                  (got_chunk_id == chunk_id) &&
                  (got_total_chunks == total_chunks) &&
                  (got_frame_deadline == frame_deadline);

    if (passed) {
        cout << GREEN << "✓ TEST 1 PASSED" << RESET << endl;
    } else {
        cout << RED << "✗ TEST 1 FAILED" << RESET << endl;
    }

    return passed;
}

bool test_boundary_values() {
    cout << "\n[TEST 2] Boundary Value Testing\n";
    cout << "================================\n";

    bool all_passed = true;

    // Test frame_id boundaries (16 bits: 0-65535)
    {
        CPacket pkt;

        cout << "Testing frame_id boundaries (16 bits: 0-65535)...\n";

        // Min value
        pkt.setFrameID(0);
        if (pkt.getFrameID() != 0) {
            cout << RED << "  ✗ frame_id=0 failed" << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ frame_id=0 passed" << RESET << endl;
        }

        // Max value (65535 = 0xFFFF)
        pkt.setFrameID(65535);
        if (pkt.getFrameID() != 65535) {
            cout << RED << "  ✗ frame_id=65535 failed, got " << pkt.getFrameID() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ frame_id=65535 passed" << RESET << endl;
        }

        // Overflow test (should wrap to 16 bits)
        pkt.setFrameID(65536);  // Should become 0
        if (pkt.getFrameID() != 0) {
            cout << RED << "  ✗ frame_id overflow test failed, got " << pkt.getFrameID() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ frame_id overflow (65536→0) passed" << RESET << endl;
        }
    }

    // Test chunk_id boundaries (8 bits: 0-255)
    {
        CPacket pkt;

        cout << "Testing chunk_id boundaries (8 bits: 0-255)...\n";

        pkt.setChunkID(0);
        if (pkt.getChunkID() != 0) {
            cout << RED << "  ✗ chunk_id=0 failed" << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ chunk_id=0 passed" << RESET << endl;
        }

        pkt.setChunkID(255);
        if (pkt.getChunkID() != 255) {
            cout << RED << "  ✗ chunk_id=255 failed, got " << pkt.getChunkID() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ chunk_id=255 passed" << RESET << endl;
        }

        pkt.setChunkID(256);  // Should become 0
        if (pkt.getChunkID() != 0) {
            cout << RED << "  ✗ chunk_id overflow test failed, got " << pkt.getChunkID() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ chunk_id overflow (256→0) passed" << RESET << endl;
        }
    }

    // Test total_chunks boundaries (8 bits: 0-255)
    {
        CPacket pkt;

        cout << "Testing total_chunks boundaries (8 bits: 0-255)...\n";

        pkt.setTotalChunks(0);
        if (pkt.getTotalChunks() != 0) {
            cout << RED << "  ✗ total_chunks=0 failed" << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ total_chunks=0 passed" << RESET << endl;
        }

        pkt.setTotalChunks(255);
        if (pkt.getTotalChunks() != 255) {
            cout << RED << "  ✗ total_chunks=255 failed, got " << pkt.getTotalChunks() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ total_chunks=255 passed" << RESET << endl;
        }

        pkt.setTotalChunks(256);  // Should become 0
        if (pkt.getTotalChunks() != 0) {
            cout << RED << "  ✗ total_chunks overflow test failed, got " << pkt.getTotalChunks() << RESET << endl;
            all_passed = false;
        } else {
            cout << GREEN << "  ✓ total_chunks overflow (256→0) passed" << RESET << endl;
        }
    }

    if (all_passed) {
        cout << GREEN << "✓ TEST 2 PASSED" << RESET << endl;
    } else {
        cout << RED << "✗ TEST 2 FAILED" << RESET << endl;
    }

    return all_passed;
}

bool test_no_bit_overlap() {
    cout << "\n[TEST 3] No Bit Overlap Test\n";
    cout << "=============================\n";

    CPacket pkt;

    // Set all frame metadata to max values
    pkt.setFrameID(65535);     // 0xFFFF
    pkt.setChunkID(255);       // 0xFF
    pkt.setTotalChunks(255);   // 0xFF
    pkt.setFrameDeadline(0xFFFFFFFF);

    cout << "Set all to max values:\n";
    cout << "  frame_id=65535, chunk_id=255, total_chunks=255, deadline=4294967295\n";

    // Verify all values
    bool passed = (pkt.getFrameID() == 65535) &&
                  (pkt.getChunkID() == 255) &&
                  (pkt.getTotalChunks() == 255) &&
                  ((uint32_t)pkt.getFrameDeadline() == 0xFFFFFFFF);

    cout << "Retrieved:\n";
    cout << "  frame_id=" << pkt.getFrameID()
         << ", chunk_id=" << pkt.getChunkID()
         << ", total_chunks=" << pkt.getTotalChunks()
         << ", deadline=" << pkt.getFrameDeadline() << endl;

    // Now change just frame_id and verify others don't change
    pkt.setFrameID(12345);

    bool no_overlap = (pkt.getFrameID() == 12345) &&
                      (pkt.getChunkID() == 255) &&  // Should remain unchanged
                      (pkt.getTotalChunks() == 255) &&  // Should remain unchanged
                      ((uint32_t)pkt.getFrameDeadline() == 0xFFFFFFFF);  // Should remain unchanged

    cout << "Changed frame_id to 12345:\n";
    cout << "  frame_id=" << pkt.getFrameID()
         << ", chunk_id=" << pkt.getChunkID()
         << " (should still be 255)"
         << ", total_chunks=" << pkt.getTotalChunks()
         << " (should still be 255)"
         << ", deadline=" << pkt.getFrameDeadline()
         << " (should still be 4294967295)" << endl;

    if (passed && no_overlap) {
        cout << GREEN << "✓ TEST 3 PASSED (no bit overlap detected)" << RESET << endl;
    } else {
        cout << RED << "✗ TEST 3 FAILED (bit overlap detected!)" << RESET << endl;
    }

    return passed && no_overlap;
}

bool test_preserve_udt_fields() {
    cout << "\n[TEST 4] Preserve UDT Original Fields\n";
    cout << "======================================\n";

    CPacket pkt;

    // Set frame metadata
    pkt.setFrameID(100);
    pkt.setChunkID(50);
    pkt.setTotalChunks(75);

    cout << "Set frame metadata: frame_id=100, chunk_id=50, total_chunks=75\n";

    // Verify that UDT's message boundary and order flags are still accessible
    // (bits 29-31 should be preserved)
    int boundary = pkt.getMsgBoundary();
    bool order_flag = pkt.getMsgOrderFlag();

    cout << "UDT fields still accessible:\n";
    cout << "  Message boundary: " << boundary << endl;
    cout << "  Order flag: " << (order_flag ? "true" : "false") << endl;

    // Frame metadata should still be correct
    bool frame_ok = (pkt.getFrameID() == 100) &&
                    (pkt.getChunkID() == 50) &&
                    (pkt.getTotalChunks() == 75);

    if (frame_ok) {
        cout << GREEN << "✓ TEST 4 PASSED (UDT fields preserved)" << RESET << endl;
    } else {
        cout << RED << "✗ TEST 4 FAILED" << RESET << endl;
    }

    return frame_ok;
}

int main() {
    cout << "\n";
    cout << "========================================\n";
    cout << "  Frame Metadata Test Suite\n";
    cout << "========================================\n";

    int passed = 0;
    int total = 4;

    if (test_basic_set_get()) passed++;
    if (test_boundary_values()) passed++;
    if (test_no_bit_overlap()) passed++;
    if (test_preserve_udt_fields()) passed++;

    cout << "\n";
    cout << "========================================\n";
    cout << "  Test Summary\n";
    cout << "========================================\n";
    cout << "Tests passed: " << passed << "/" << total << endl;

    if (passed == total) {
        cout << GREEN << "✓ ALL TESTS PASSED!" << RESET << endl;
        cout << "\nFrame metadata implementation is working correctly.\n";
        return 0;
    } else {
        cout << RED << "✗ SOME TESTS FAILED" << RESET << endl;
        cout << "\nPlease fix the issues before proceeding.\n";
        return 1;
    }
}
