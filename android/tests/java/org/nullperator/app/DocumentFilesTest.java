package org.nullperator.app;
import java.io.*;
import java.nio.file.*;

public final class DocumentFilesTest {
    interface Operation { void run() throws Exception; }
    static void rejected(Operation work) throws Exception {
        try { work.run(); throw new AssertionError("Expected rejection"); } catch (IOException expected) { }
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("tracker-documents");
        try {
            DocumentFiles docs = new DocumentFiles(root.toFile());
            docs.create("root", "samples", true);
            File sample = docs.create("root/samples", "kick.wav", false);
            assert docs.id(sample).equals("root/samples/kick.wav");
            rejected(() -> docs.create("root/samples", "KICK.WAV", false));
            rejected(() -> docs.resolve("root/../secret"));
            rejected(() -> docs.resolve("root/samples//kick.wav"));
            rejected(() -> docs.resolve("root/.import-secret"));
            rejected(() -> docs.create("root", "../escape", false));
            rejected(() -> docs.rename("root", "renamed"));
            rejected(() -> docs.delete("root"));
            Files.createSymbolicLink(root.resolve("link"), root.resolve("samples"));
            rejected(() -> docs.resolve("root/link/kick.wav"));
            Files.delete(root.resolve("link"));
            File renamed = docs.rename("root/samples/kick.wav", "snare.wav");
            assert renamed.exists() && !sample.exists();
            docs.delete("root/samples");
            assert !root.resolve("samples").toFile().exists();
            System.out.println("DocumentFiles tests passed");
        } finally { try (var paths = Files.walk(root)) { for (Path p : paths.sorted(java.util.Comparator.reverseOrder()).toList()) Files.deleteIfExists(p); } }
    }
}
