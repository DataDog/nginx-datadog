"""Installer acceptance tests and local examples."""


def load_tests(loader, tests, pattern):
    """Keep pytest cases out of the existing unittest suite."""
    return tests
