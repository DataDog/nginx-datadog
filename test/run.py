import sys
import unittest
import argparse
from runner import xmlrunner

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-r", "--reporter", choices=("console", "xml"), default="console"
    )
    parser.add_argument(
        "-f",
        "--failfast",
        dest="failfast",
        action="store_true",
        help="Stop on first fail or error",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        dest="verbosity",
        action="store_const",
        const=2,
        help="Verbose output",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        dest="verbosity",
        action="store_const",
        const=0,
        help="Quiet output",
    )

    args = parser.parse_args()

    loader = unittest.TestLoader()
    tests = loader.discover(".")

    runner_args = {
        "failfast": args.failfast,
        "verbosity": args.verbosity if args.verbosity else 1,
    }

    success = 0
    if args.reporter == "console":
        runner = unittest.runner.TextTestRunner(**runner_args)
        success = runner.run(tests).wasSuccessful()
    elif args.reporter == "xml":
        with open("results.xml", "wb") as output:
            runner = xmlrunner.XMLTestRunner(output=output, **runner_args)
            success = runner.run(tests).wasSuccessful()

    sys.exit(0) if success else sys.exit(1)
