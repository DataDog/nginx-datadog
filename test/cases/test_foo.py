import case


class TestFoo(case.TestCase):
    def test_foo(self):
        self.assertEqual(1, 1)

    def test_foo2(self):
        self.assertEqual(2, 2)
