"""Service orchestration (docker-compose) facilities for testing"""

from lazy_singleton import LazySingleton

import contextlib
import queue
import threading
import unittest


class Orchestration:
    """TODO"""

    # Properties (all private)
    # - `up_thread` is the `threading.Thread` running `docker-compose up`.
    # - `logs` is a `dict` that maps service name to a `queue.Queue` of log
    #   lines.

    def up(self):
        """Start service orchestration.

        Run `docker-compose down` to bring up the orchestrated services.
        Begin parsing their logs on a separate thread.
        """
        # TODO
        
    def down(self):
        """Stop service orchestration.
        
        Run `docker-compose down` to bring down the orchestrated services.
        Join the log-parsing thread.
        """
        # TODO

    def sync_proxied_services(self):
        """Establish synchronization points in the logs of proxied services.
        
        Send a "sync" request to each of the services reverse proxied by
        nginx, and wait for the corresponding log messages to appear in the
        docker-compose log.  This way, we know that whatever we have done
        previously has already appeared in the log.
        
            logs_by_service = orch.sync_proxied_services()

        where `logs_by_service` is a `dict` mapping service name to a
        chronological list of log lines gathered since the previous sync.
        """
        # TODO
    
    def nginx_test_config(self, nginx_conf_text):
        """Test an nginx configuration.

        Write the specified `nginx_conf_text` to a file in the nginx
        container and tell nginx to check the config as if it were loading it.
        Return `(status, log_lines)`, where `status` is the integer status of
        the nginx check command, and `log_lines` is a chronological list of
        lines from the ouptut of the command.
        """
        # TODO
    
    def nginx_reset(self):
        """Restore nginx to a default configuration.

        Overwrite nginx's config with a default, send it a "/sync" request,
        and wait for the corresponding log message to appear in the access
        log.
        """
        # TODO

    def nginx_replace_config(self, nginx_conf_text):
        """Replace nginx's config and reload nginx.

        Call `self.nginx_test_config(nginx_conf_text)`.  If the resulting
        status code is zero (success), overwrite nginx's config with
        `nginx_conf_text` and reload nginx.  Return the `(status, log_lines)`
        returned by the call to `nginx_test_config`.
        """
        # TODO


_singleton = LazySingleton(Orchestration, Orchestration.up, Orchestration.down)


def singleton():
    """Return a context manager providing access to the singleton instance of
    `Orchestration`.

    This is meant for use in `with` statements, e.g.

        with orchestration.singleton() as orch:
            status, log_lines = orch.nginx_test_config(...)
    """
    return _singleton.context()
