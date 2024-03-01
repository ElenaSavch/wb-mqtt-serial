#include "devices/curtains/a_ok_device.h"
#include "test_utils.h"
#include <wblib/testing/common.h>

using namespace Aok;
using namespace WBMQTT::Testing;

TEST(TAokTest, MakeRequest)
{
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x10, 0x2, 0x3, 0x0a, 0xdd), {0x9a, 0x10, 0x2, 0x3, 0x0a, 0xdd, 0xc6}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0xcc), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0xcc, 0xc6}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0xee), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0xee, 0xe4}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x0d), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x0d, 0x07}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x0e), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x0e, 0x04}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x01), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x01, 0x0b}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x02), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x02, 0x08}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x03), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x03, 0x09}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0, 0x0, 0x0, 0x0a, 0x04), {0x9a, 0x0, 0x0, 0x0, 0x0a, 0x04, 0x0e}));
}