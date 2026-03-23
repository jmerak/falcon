/**
 * Standalone verification test for communication statistics counters.
 * Creates two CommPartyTCPSynced endpoints, sends known data between them,
 * and asserts that bytesIn/bytesOut/sendCount/recvCount are accurate.
 *
 * Compile (inside Docker build env):
 *   g++ -std=c++11 -I/workspace/include -o test_comm_stats \
 *       test/falcon/test_comm_stats.cc src/executor/network/Comm.cpp \
 *       -lboost_system -lpthread
 *
 * Or run via CTest after adding to CMakeLists.
 */

#include "falcon/network/Comm.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

static void test_basic_write_read() {
  std::cout << "=== Test 1: basic write/read counters ===" << std::endl;

  boost::asio::io_service io1, io2;
  // Party A listens on 19001, connects to 19002
  // Party B listens on 19002, connects to 19001
  SocketPartyData addrA(boost_ip::address::from_string("127.0.0.1"), 19001);
  SocketPartyData addrB(boost_ip::address::from_string("127.0.0.1"), 19002);

  CommPartyTCPSynced channelA(io1, addrA, addrB);  // role=2 (both)
  CommPartyTCPSynced channelB(io2, addrB, addrA);  // role=2 (both)

  // Connect in separate threads (join blocks until both sides connect)
  std::thread tA([&]() { channelA.join(200, 5000); });
  std::thread tB([&]() { channelB.join(200, 5000); });
  tA.join();
  tB.join();
  std::cout << "  Connected." << std::endl;

  // --- Send 100 bytes from A to B ---
  std::vector<byte> sendBuf(100, 0xAB);
  std::vector<byte> recvBuf(100, 0);

  channelA.write(sendBuf.data(), 100);
  channelB.read(recvBuf.data(), 100);

  assert(channelA.bytesOut == 100);
  assert(channelA.sendCount == 1);
  assert(channelB.bytesIn == 100);
  assert(channelB.recvCount == 1);
  // Verify data integrity
  assert(memcmp(sendBuf.data(), recvBuf.data(), 100) == 0);
  std::cout << "  A->B 100 bytes: PASS" << std::endl;

  // --- Send 256 bytes from B to A ---
  std::vector<byte> sendBuf2(256, 0xCD);
  std::vector<byte> recvBuf2(256, 0);

  channelB.write(sendBuf2.data(), 256);
  channelA.read(recvBuf2.data(), 256);

  assert(channelB.bytesOut == 256);
  assert(channelB.sendCount == 1);
  assert(channelA.bytesIn == 256);
  assert(channelA.recvCount == 1);
  assert(memcmp(sendBuf2.data(), recvBuf2.data(), 256) == 0);
  std::cout << "  B->A 256 bytes: PASS" << std::endl;

  std::cout << "  Test 1 PASSED." << std::endl;
}

static void test_multiple_ops() {
  std::cout << "=== Test 2: multiple operations accumulate ===" << std::endl;

  boost::asio::io_service io1, io2;
  SocketPartyData addrA(boost_ip::address::from_string("127.0.0.1"), 19003);
  SocketPartyData addrB(boost_ip::address::from_string("127.0.0.1"), 19004);

  CommPartyTCPSynced channelA(io1, addrA, addrB);
  CommPartyTCPSynced channelB(io2, addrB, addrA);

  std::thread tA([&]() { channelA.join(200, 5000); });
  std::thread tB([&]() { channelB.join(200, 5000); });
  tA.join();
  tB.join();

  // Send 10 messages of varying sizes from A to B
  long expectedBytesOut = 0;
  for (int i = 1; i <= 10; i++) {
    int sz = i * 50;  // 50, 100, ..., 500
    std::vector<byte> buf(sz, (byte)i);
    channelA.write(buf.data(), sz);
    expectedBytesOut += sz;

    std::vector<byte> rbuf(sz, 0);
    channelB.read(rbuf.data(), sz);
    // Verify data
    for (int j = 0; j < sz; j++) {
      assert(rbuf[j] == (byte)i);
    }
  }

  assert(channelA.bytesOut == expectedBytesOut);
  assert(channelA.sendCount == 10);
  assert(channelB.bytesIn == expectedBytesOut);
  assert(channelB.recvCount == 10);
  std::cout << "  10 messages, total " << expectedBytesOut
            << " bytes: PASS" << std::endl;

  // Also verify A's recv and B's send are still 0
  assert(channelA.bytesIn == 0);
  assert(channelA.recvCount == 0);
  assert(channelB.bytesOut == 0);
  assert(channelB.sendCount == 0);
  std::cout << "  Reverse direction counters are 0: PASS" << std::endl;

  std::cout << "  Test 2 PASSED." << std::endl;
}

static void test_writeWithSize() {
  std::cout << "=== Test 3: writeWithSize/readWithSizeIntoVector ===" << std::endl;

  boost::asio::io_service io1, io2;
  SocketPartyData addrA(boost_ip::address::from_string("127.0.0.1"), 19005);
  SocketPartyData addrB(boost_ip::address::from_string("127.0.0.1"), 19006);

  CommPartyTCPSynced channelA(io1, addrA, addrB);
  CommPartyTCPSynced channelB(io2, addrB, addrA);

  std::thread tA([&]() { channelA.join(200, 5000); });
  std::thread tB([&]() { channelB.join(200, 5000); });
  tA.join();
  tB.join();

  // writeWithSize sends: 4-byte size header + data
  std::string msg = "Hello, Falcon communication stats test!";
  channelA.writeWithSize(msg);

  std::vector<byte> received;
  channelB.readWithSizeIntoVector(received);
  std::string received_str(received.begin(), received.end());

  assert(received_str == msg);

  // writeWithSize calls write twice: once for size (4 bytes), once for data
  int expectedOut = (int)(sizeof(int) + msg.size());
  assert(channelA.bytesOut == expectedOut);
  assert(channelA.sendCount == 2);  // two write() calls internally

  // readWithSizeIntoVector calls readSize() [which calls read(4)] + read(msgSize)
  assert(channelB.bytesIn == expectedOut);
  assert(channelB.recvCount == 2);  // two read() calls internally

  std::cout << "  writeWithSize '" << msg << "' (" << expectedOut
            << " bytes): PASS" << std::endl;
  std::cout << "  Test 3 PASSED." << std::endl;
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "Communication Statistics Verification" << std::endl;
  std::cout << "========================================" << std::endl;

  test_basic_write_read();
  test_multiple_ops();
  test_writeWithSize();

  std::cout << "========================================" << std::endl;
  std::cout << "ALL TESTS PASSED!" << std::endl;
  std::cout << "========================================" << std::endl;
  return 0;
}
