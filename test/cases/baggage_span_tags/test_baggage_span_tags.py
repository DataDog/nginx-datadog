from .. import case
from .. import formats

from pathlib import Path
import pprint


class TestBaggageSpanTags(case.TestCase):

    def run_custom_tags_test(self, conf_relative_path, http_path,
                             configured_baggage_span_tags):
        """Verify that spans produced by an nginx configured using the
        specified nginx `conf_text` (from a file having the specified
        `file_name`) contain expected values for the baggage span tags.
        """
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        # To test this, we make any old request to nginx, and then in order
        # to ensure that nginx flushes its trace to the agent, reload nginx.
        # Then we send a "sync" request to the agent in order to establish a
        # log line that's strictly after the trace was flushed, and finally we
        # examine the interim log lines from the agent to find the tags sent to
        # it by nginx's tracer.
        self.orch.nginx_replace_config(conf_text, conf_path.name)

        # Consume any previous logging from the agent.
        self.orch.sync_service("agent")

        headers = {
            "baggage":
            "user.id=doggo,session.id=123,account.id=456,snazzy.tag=hard-coded,fancy.tag=GET"
        }

        status, _, _ = self.orch.send_nginx_http_request(http_path,
                                                         headers=headers)
        self.assertEqual(status, 200, conf_relative_path)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")

        for line in log_lines:
            segments = formats.parse_trace(line)
            if segments is None:
                # some other kind of logging; ignore
                continue
            for segment in segments:
                for span in segment:
                    if span["service"] != "nginx":
                        continue
                    # We found an nginx span.  Make sure that it has the
                    # "baggage.snazzy.tag" and "baggage.fancy.tag" tags, with the expected values.
                    # The two tags are assumed to be configured in `conf_text`.
                    tags = span["meta"]

                    if (configured_baggage_span_tags and "user.id" in configured_baggage_span_tags):
                        self.assertIn("baggage.user.id", tags,
                                      conf_relative_path)
                        self.assertEqual(tags["baggage.user.id"], "doggo",
                                         conf_relative_path)
                    else:
                        self.assertNotIn("baggage.user.id", tags,
                                         conf_relative_path)

                    if (configured_baggage_span_tags and "session.id" in configured_baggage_span_tags):
                        self.assertIn("baggage.session.id", tags,
                                      conf_relative_path)
                        self.assertEqual(tags["baggage.session.id"], "123",
                                         conf_relative_path)
                    else:
                        self.assertNotIn("baggage.session.id", tags,
                                         conf_relative_path)

                    if (configured_baggage_span_tags and "account.id" in configured_baggage_span_tags):
                        self.assertIn("baggage.account.id", tags,
                                      conf_relative_path)
                        self.assertEqual(tags["baggage.account.id"], "456",
                                         conf_relative_path)
                    else:
                        self.assertNotIn("baggage.account.id", tags,
                                         conf_relative_path)

                    if (configured_baggage_span_tags and "snazzy.tag" in configured_baggage_span_tags):
                        self.assertIn("baggage.snazzy.tag", tags,
                                      conf_relative_path)
                        self.assertEqual(tags["baggage.snazzy.tag"],
                                         "hard-coded", conf_relative_path)
                    else:
                        self.assertNotIn("baggage.snazzy.tag", tags,
                                         conf_relative_path)

                    if (configured_baggage_span_tags and "fancy.tag" in configured_baggage_span_tags):
                        self.assertIn("baggage.fancy.tag", tags,
                                      conf_relative_path)
                        self.assertEqual(tags["baggage.fancy.tag"], "GET",
                                         conf_relative_path)
                    else:
                        self.assertNotIn("baggage.fancy.tag", tags,
                                         conf_relative_path)

    def test_custom_in_location(self):
        return self.run_custom_tags_test("./conf/custom_in_location.conf",
                                         "/http", ("snazzy.tag", "fancy.tag"))

    def test_custom_in_server(self):
        return self.run_custom_tags_test("./conf/custom_in_server.conf",
                                         "/http", ("snazzy.tag", "fancy.tag"))

    def test_custom_in_http(self):
        return self.run_custom_tags_test("./conf/custom_in_http.conf", "/http",
                                         ("snazzy.tag", "fancy.tag"))

    def test_default_tags(self):
        return self.run_custom_tags_test(
            "./conf/builtins.conf", "/http",
            ("user.id", "session.id", "account.id"))

    def test_main_disabled(self):
        return self.run_custom_tags_test("./conf/disabled.conf", "/http", None)

    def test_overwite_default_tags_empty_inherits_previous_conf(self):
        return self.run_custom_tags_test(
            "./conf/builtins.conf", "/http",
            ("user.id", "session.id", "account.id"))

    def test_overwite_default_tags_disabled_at_location(self):
        return self.run_custom_tags_test("./conf/overwrite_defaults.conf",
                                         "/disabled_tags", None)

    def test_overwite_all_baggage_span_tags_using_wildcard(self):
        return self.run_custom_tags_test(
            "./conf/overwrite_defaults.conf", "/all_baggage_span_tags",
            ("user.id", "session.id", "account.id", "snazzy.tag", "fancy.tag"))

    def test_overwite_default_tags_custom_ignores_previous_conf(self):
        return self.run_custom_tags_test("./conf/overwrite_defaults.conf",
                                         "/snazzy_tag", ("snazzy.tag"))
