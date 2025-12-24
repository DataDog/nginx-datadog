import java.io.*;
import java.nio.file.*;
import java.sql.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.regex.*;
import java.util.stream.*;

public class LogAnalyzer {
    private static final int CHUNK_SIZE = 10000;
    private static final int THREAD_COUNT = Runtime.getRuntime().availableProcessors();

    private final Connection conn;
    private final Pattern messagePattern = Pattern.compile("\\d+#\\d+:\\s+\\*?\\d*\\s+(.*)");
    private final Pattern messagePattern2 = Pattern.compile("\\d+#\\d+:\\s+(.*)");

    public LogAnalyzer(String dbPath) throws SQLException {
        conn = DriverManager.getConnection("jdbc:sqlite:" + dbPath);
        initDb();
    }

    private void initDb() throws SQLException {
        try (Statement stmt = conn.createStatement()) {
            stmt.execute(
                "CREATE TABLE IF NOT EXISTS log_messages (" +
                "    id INTEGER PRIMARY KEY AUTOINCREMENT," +
                "    message TEXT UNIQUE NOT NULL," +
                "    seen_on_log INTEGER DEFAULT 0," +
                "    seen_on_log_count INTEGER DEFAULT 0," +
                "    seen_on_docker INTEGER DEFAULT 0" +
                ")"
            );
        }
    }

    private String generifyMessage(String line) {
        if (line.trim().startsWith("{")) return null;

        Matcher m = messagePattern.matcher(line);
        if (!m.find()) {
            m = messagePattern2.matcher(line);
            if (!m.find()) return null;
        }

        String msg = m.group(1).trim();
        if (msg.isEmpty()) return null;

        // Setting tag with value
        msg = msg.replaceAll("(Setting tag \\S+ with value\\s+).*", "$1<VALUE>");

        // Special case: http proxy header with multiple nested quotes - collapse to single <STR>
        if (msg.startsWith("http proxy header:")) {
            msg = "http proxy header: <STR>";
        }

        // Quoted strings
        msg = msg.replaceAll("'[^']*'", "'<STR>'");
        msg = msg.replaceAll("\"[^\"]*\"", "\"<STR>\"");

        // Hex addresses
        msg = msg.replaceAll("\\b0x[0-9A-Fa-f]+\\b", "<HEX>");
        msg = msg.replaceAll("\\b[0-9A-Fa-f]{12,}\\b", "<HEX>");

        // Numbers
        msg = msg.replaceAll("\\b\\d{6,}\\b", "<NUM>");
        msg = msg.replaceAll("\\b\\d+\\b", "<N>");
        msg = msg.replaceAll("-<N>", "<N>");

        // IP addresses
        msg = msg.replaceAll("\\b\\d+\\.\\d+\\.\\d+\\.\\d+\\b", "<IP>");

        // Clean up
        msg = msg.replaceAll("\\s+", " ").trim();
        msg = msg.replaceAll("(<N>\\s*[:\\-,]\\s*)+", "<N>:");
        msg = msg.replaceAll("(<HEX>\\s*)+", "<HEX> ");

        return msg;
    }

    public Map<String, Integer> processFile(String filePath) throws IOException {
        System.out.println("Processing " + filePath + "...");

        List<String> lines = Files.readAllLines(Paths.get(filePath));
        ConcurrentHashMap<String, Integer> messageCounts = new ConcurrentHashMap<>();
        ExecutorService executor = Executors.newFixedThreadPool(THREAD_COUNT);

        // Split into chunks and process in parallel
        int totalChunks = (lines.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
        List<Future<?>> futures = new ArrayList<>();

        for (int i = 0; i < totalChunks; i++) {
            final int start = i * CHUNK_SIZE;
            final int end = Math.min(start + CHUNK_SIZE, lines.size());

            futures.add(executor.submit(() -> {
                for (int j = start; j < end; j++) {
                    String msg = generifyMessage(lines.get(j));
                    if (msg != null) {
                        messageCounts.merge(msg, 1, Integer::sum);
                    }
                }
            }));
        }

        // Wait for all chunks to complete
        for (Future<?> future : futures) {
            try {
                future.get();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        executor.shutdown();
        System.out.println("  Found " + messageCounts.size() + " unique patterns");

        return messageCounts;
    }

    public void updateDatabase(Map<String, Integer> messages, String source) throws SQLException {
        System.out.println("Updating database for source: " + source);

        conn.setAutoCommit(false);

        if (source.equals("log")) {
            String sql = "INSERT INTO log_messages (message, seen_on_log, seen_on_log_count) " +
                "VALUES (?, 1, ?) " +
                "ON CONFLICT(message) DO UPDATE SET " +
                "    seen_on_log = 1, " +
                "    seen_on_log_count = seen_on_log_count + excluded.seen_on_log_count";

            try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
                for (Map.Entry<String, Integer> entry : messages.entrySet()) {
                    pstmt.setString(1, entry.getKey());
                    pstmt.setInt(2, entry.getValue());
                    pstmt.addBatch();
                }
                pstmt.executeBatch();
            }
        } else {
            String sql = "INSERT INTO log_messages (message, seen_on_docker) " +
                "VALUES (?, 1) " +
                "ON CONFLICT(message) DO UPDATE SET seen_on_docker = 1";

            try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
                for (String msg : messages.keySet()) {
                    pstmt.setString(1, msg);
                    pstmt.addBatch();
                }
                pstmt.executeBatch();
            }
        }

        conn.commit();
        conn.setAutoCommit(true);
        System.out.println("Database updated");
    }

    public void printStats() throws SQLException {
        try (Statement stmt = conn.createStatement()) {
            ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages");
            int total = rs.getInt(1);

            rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages WHERE seen_on_log = 1");
            int onLog = rs.getInt(1);

            rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages WHERE seen_on_docker = 1");
            int onDocker = rs.getInt(1);

            rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages WHERE seen_on_log = 1 AND seen_on_docker = 1");
            int both = rs.getInt(1);

            rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages WHERE seen_on_log = 1 AND seen_on_docker = 0");
            int onlyLog = rs.getInt(1);

            rs = stmt.executeQuery("SELECT COUNT(*) FROM log_messages WHERE seen_on_docker = 1 AND seen_on_log = 0");
            int onlyDocker = rs.getInt(1);

            System.out.println("\n" + "=".repeat(60));
            System.out.println("LOG MESSAGE STATISTICS");
            System.out.println("=".repeat(60));
            System.out.println("Total unique patterns: " + total);
            System.out.println("Seen in original log: " + onLog);
            System.out.println("Seen in docker logs: " + onDocker);
            System.out.println("Only in original (missing from docker): " + onlyLog);
            System.out.println("Only in docker: " + onlyDocker);
            System.out.println("Seen in both: " + both);

            if (onLog > 0) {
                int coverage = (both * 100) / onLog;
                System.out.println("\nCoverage: " + both + "/" + onLog + " = " + coverage + "%");
            }
            System.out.println("=".repeat(60) + "\n");
        }
    }

    public void showMissingPatterns(int limit) throws SQLException {
        String sql = "SELECT message, seen_on_log_count FROM log_messages " +
                     "WHERE seen_on_log = 1 AND seen_on_docker = 0 " +
                     "ORDER BY seen_on_log_count DESC LIMIT ?";

        try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
            pstmt.setInt(1, limit);
            ResultSet rs = pstmt.executeQuery();

            System.out.println("TOP " + limit + " MISSING PATTERNS (in original, not in docker)");
            System.out.println("=".repeat(80));

            int count = 1;
            while (rs.next()) {
                String message = rs.getString("message");
                int occurrences = rs.getInt("seen_on_log_count");
                System.out.printf("%2d. [%3dx] %s%n", count++, occurrences, message);
            }
            System.out.println("=".repeat(80) + "\n");
        }
    }

    public void close() throws SQLException {
        if (conn != null) conn.close();
    }

    public static void main(String[] args) {
        if (args.length < 1) {
            System.out.println("Usage:");
            System.out.println("  java LogAnalyzer <original_log> <docker_log>");
            System.out.println("  java LogAnalyzer stats");
            System.exit(1);
        }

        try {
            LogAnalyzer analyzer = new LogAnalyzer("log_messages.db");

            if (args[0].equals("stats")) {
                analyzer.printStats();
                analyzer.showMissingPatterns(50);
            } else {
                long start = System.currentTimeMillis();

                // Process original log
                Map<String, Integer> originalMessages = analyzer.processFile(args[0]);
                analyzer.updateDatabase(originalMessages, "log");

                // Process docker log if provided
                if (args.length > 1) {
                    Map<String, Integer> dockerMessages = analyzer.processFile(args[1]);
                    analyzer.updateDatabase(dockerMessages, "docker");
                }

                analyzer.printStats();
                analyzer.showMissingPatterns(50);

                long elapsed = System.currentTimeMillis() - start;
                System.out.println("Total time: " + (elapsed / 1000.0) + "s");
            }

            analyzer.close();
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(1);
        }
    }
}
