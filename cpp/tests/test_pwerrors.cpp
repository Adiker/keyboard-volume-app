#include <gtest/gtest.h>

// Exercise the actual PipeWire callback without connecting to the desktop server.
#include "../src/pwutils.cpp"

TEST(PipeWireErrors, ObjectErrorDoesNotPoisonSession)
{
    PipeWireSession session;
    session.loop = pw_thread_loop_new("pw-error-test", nullptr);
    ASSERT_NE(session.loop, nullptr);
    session.activeProxyId = 42;
    PipeWireSession::onCoreError(&session, 42, 0, -ENOENT, "unsupported parameter");
    EXPECT_TRUE(session.activeProxyError);
    EXPECT_FALSE(session.coreError.load());
}

TEST(PipeWireErrors, UnrelatedObjectErrorDoesNotFailActiveOperation)
{
    PipeWireSession session;
    session.loop = pw_thread_loop_new("pw-error-test", nullptr);
    ASSERT_NE(session.loop, nullptr);
    session.activeProxyId = 42;
    PipeWireSession::onCoreError(&session, 43, 0, -ENOENT, "object disappeared");
    EXPECT_FALSE(session.activeProxyError);
    EXPECT_FALSE(session.coreError.load());
}

TEST(PipeWireErrors, CoreAndConnectionErrorsInvalidateSession)
{
    PipeWireSession session;
    session.loop = pw_thread_loop_new("pw-error-test", nullptr);
    ASSERT_NE(session.loop, nullptr);
    PipeWireSession::onCoreError(&session, PW_ID_CORE, 0, -EINVAL, "core error");
    EXPECT_TRUE(session.coreError.load());
    session.coreError.store(false);
    PipeWireSession::onCoreError(&session, 42, 0, -EPIPE, "disconnected");
    EXPECT_TRUE(session.coreError.load());
}
