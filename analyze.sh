#!/bin/bash
# Wrapper script to run the Java log analyzer

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check if SQLite JDBC driver exists
if [ ! -f "sqlite-jdbc.jar" ]; then
    echo "Downloading SQLite JDBC driver..."
    curl -L -o sqlite-jdbc.jar https://repo1.maven.org/maven2/org/xerial/sqlite-jdbc/3.47.1.0/sqlite-jdbc-3.47.1.0.jar
fi

# Compile if needed
if [ ! -f "LogAnalyzer.class" ] || [ "LogAnalyzer.java" -nt "LogAnalyzer.class" ]; then
    echo "Compiling LogAnalyzer.java..."
    javac -cp sqlite-jdbc.jar LogAnalyzer.java || exit 1
fi

# Run the analyzer
java -cp .:sqlite-jdbc.jar LogAnalyzer "$@"
