import case
import orchestration


class TestBar(case.TestCase):
    def test_bar(self):
        self.assertTrue(self.orch is not None)

    def test_bar2(self):
        # TODO: unnecessary `with` block; could use `self.orch` instead
        with orchestration.singleton() as orch:
            self.assertEqual(2, 2)
