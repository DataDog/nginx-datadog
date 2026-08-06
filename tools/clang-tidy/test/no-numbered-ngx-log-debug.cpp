#define ngx_log_debug(...) ((void)0)
#define ngx_log_debug0(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug1(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug2(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug3(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug4(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug5(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug6(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug7(...) ngx_log_debug(__VA_ARGS__)
#define ngx_log_debug8(...) ngx_log_debug(__VA_ARGS__)

void valid() { ngx_log_debug(0, nullptr, 0, "message"); }

void invalid() {
  ngx_log_debug0(0, nullptr, 0, "message");
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "message");
  ngx_log_debug1(0, nullptr, 0, "message %d", 1);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "message %d", 1);
  ngx_log_debug2(0, nullptr, 0, "%d %d", 1, 2);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d", 1, 2);
  ngx_log_debug3(0, nullptr, 0, "%d %d %d", 1, 2, 3);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d", 1, 2, 3);
  ngx_log_debug4(0, nullptr, 0, "%d %d %d %d", 1, 2, 3, 4);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d %d", 1, 2, 3, 4);
  ngx_log_debug5(0, nullptr, 0, "%d %d %d %d %d", 1, 2, 3, 4, 5);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d %d %d", 1, 2, 3, 4,
  // CHECK-FIXES-NEXT: 5);
  ngx_log_debug6(0, nullptr, 0, "%d %d %d %d %d %d", 1, 2, 3, 4, 5, 6);
  // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d %d %d %d", 1, 2, 3,
  // CHECK-FIXES-NEXT: 4, 5, 6);
  ngx_log_debug7(0, nullptr, 0, "%d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7);
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d %d %d %d %d", 1, 2,
  // CHECK-FIXES-NEXT: 3, 4, 5, 6, 7);
  ngx_log_debug8(0, nullptr, 0, "%d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7,
                 8);
  // CHECK-MESSAGES: :[[@LINE-2]]:3: warning: use variadic macro
  // CHECK-FIXES: ngx_log_debug(0, nullptr, 0, "%d %d %d %d %d %d %d %d", 1,
  // CHECK-FIXES-NEXT: 2, 3, 4, 5, 6, 7, 8);
}
