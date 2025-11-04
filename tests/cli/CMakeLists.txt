add_cli_test(cli.server.smoke tskv_server
  ARGS --dry-run
  PASS "tskv.*server.*CFG.*host=0.0.0.0.*port=7070.*wal-sync=append.*memtable-bytes=67108864.*max-connections=1024"
  LABELS "cli;server;args")

add_cli_test(cli.server.help tskv_server
  ARGS --help
  PASS "usage.*Options:.*--version.*--help"
  LABELS "cli;server;args")

add_cli_test(cli.client.smoke tskv_client
  ARGS --dry-run
  PASS "tskv.*client.*CFG.*host=127.0.0.1.*timeout-ms=2000"
  LABELS "cli;client;args")

add_cli_test(cli.client.help tskv_client
  ARGS --help
  PASS "usage.*Options:.*--version.*--help"
  LABELS "cli;client;args")

add_cli_test(cli.server.bad_port_negative tskv_server
  ARGS --port -1
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.bad_port_zero tskv_server
  ARGS --port 0
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.client.bad_port_negative tskv_client
  ARGS --port -1
  EXPECT_FAIL
  LABELS "cli;client;args"
)

add_cli_test(cli.client.bad_port_zero tskv_client
  ARGS --port 0
  EXPECT_FAIL
  LABELS "cli;client;args"
)

add_cli_test(cli.client.unknown_arg tskv_client
  ARGS --asdf 123
  EXPECT_FAIL
  LABELS "cli;client;args"
)

add_cli_test(cli.server.unknown_arg tskv_server
  ARGS --asdf 123
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.unknown_wal_sync_policty tskv_server
  ARGS --wal-sync magic
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.data_dir_noexist tskv_server
  ARGS --data-dir ./first/second/data
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.data_neg_memtable tskv_server
  ARGS --memtable-bytes -123
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.data_neg_max_connections tskv_server
  ARGS --max-connections -123
  EXPECT_FAIL
  LABELS "cli;server;args"
)

add_cli_test(cli.server.version tskv_server
  ARGS --version
  PASS "tskv.*${TSKV_PROJECT_VERSION}"
  LABELS "cli;server;args"
)

add_cli_test(cli.client.version tskv_client
  ARGS --version
  PASS "tskv.*${TSKV_PROJECT_VERSION}"
  LABELS "cli;client;args"
)
