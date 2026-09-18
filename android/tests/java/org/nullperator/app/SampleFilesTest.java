package org.nullperator.app;
import java.io.*;
import java.nio.file.*;
import java.util.Arrays;
public final class SampleFilesTest {
    interface Work { void run() throws Exception; }
    static void fails(Work work) throws Exception {
        try { work.run(); throw new AssertionError("Expected rejection"); } catch (IOException expected) {}
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("android-samples-");
        try {
            File library = root.resolve("samples").toFile(), project = root.resolve("project").toFile();
            project.mkdir();
            assert SampleFiles.filename("  Kick.WAV ").equals("Kick.wav");
            fails(() -> SampleFiles.filename("../escape"));
            fails(() -> SampleFiles.filename("bad:name"));
            fails(() -> SampleFiles.filename("测试测试测试测试"));
            byte[] wav = "RIFF\u0004\u0000\u0000\u0000WAVEdata".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            SampleFiles.importWav(new ByteArrayInputStream(wav), library, project, "Kick.wav");
            assert Arrays.equals(wav, Files.readAllBytes(library.toPath().resolve("Kick.wav")));
            fails(() -> SampleFiles.importWav(new ByteArrayInputStream(wav), library, project, "kick.wav"));
            Files.write(project.toPath().resolve("Snare.WAV"), wav);
            fails(() -> SampleFiles.importWav(new ByteArrayInputStream(wav), library, project, "snare.wav"));
            fails(() -> SampleFiles.importWav(new ByteArrayInputStream(new byte[3]), library, project, "bad.wav"));
            fails(() -> SampleFiles.importWav(new ByteArrayInputStream(wav), library, project, "../escape.wav"));
            assert library.list().length == 1;
            assert Arrays.equals(wav, Files.readAllBytes(library.toPath().resolve("Kick.wav")));
            System.out.println("Android sample import: names, duplicate protection, WAV validation and cleanup passed");
        } finally {
            try (var paths = Files.walk(root)) { for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList()) Files.delete(path); }
        }
    }
}
