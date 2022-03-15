"""Reference counted singleton instances"""


import contextlib


class LazySingleton:
    """This is a reference counted, lazily initialized instance of some object.

    The means to "make" an object, "start" it, and "stop" it are provided as
    constructor arguments.

    The instance is accessed via a `with` statement using the `context` method,
    e.g.

        with some_singleton.context() as instance:
            print('leased a reference to', instance)
    """
    def __init__(self, make, start, stop):
        """
        `make()` returns an instance.
        `start(instance)` starts the instance.
        `stop(instance)` stops the instance.
        """
        self.make_instance = make
        self.start_instance = start
        self.stop_instance = stop
        self.reference_count = 0
        self.instance = None

    @contextlib.contextmanager
    def context(self):
        self.increment_reference_count()
        yield self.instance
        self.decrement_reference_count()

    def increment_reference_count(self):
        self.reference_count += 1
        if self.reference_count == 1:
            self.instance = self.make_instance()
            self.start_instance(self.instance)

    def decrement_reference_count(self):
        if self.reference_count == 0:
            raise Exception('reference count is already zero')
        self.reference_count -= 1
        if self.reference_count == 0:
            self.stop_instance(self.instance)
