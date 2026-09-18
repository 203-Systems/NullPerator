package org.nullperator.app;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Filesystem-only import logic; also exercised by the JVM host tests. */
final class SampleFiles {
    static String filename(String input) throws IOException {
        String value = input.trim().replaceFirst("(?i)\\.wav$", "");
        if (value.isEmpty() || value.startsWith(".") || value.getBytes(StandardCharsets.UTF_8).length > 20
                || value.matches(".*[/\\\\:\\p{Cntrl}].*"))
            throw new IOException("Use 1–20 bytes without path characters");
        return value + ".wav";
    }
    static boolean exists(File directory, String name) {
        String[] files = directory.list();
        if (files != null) for (String file : files) if (file.equalsIgnoreCase(name)) return true;
        return false;
    }
    static void importWav(InputStream source, File library, File project, String filename) throws IOException {
        File temporary = null;
        try (InputStream in = source) {
            if (in == null) throw new IOException("Cannot open the selected file");
            if (!filename.equals(filename(filename))) throw new IOException("Invalid sample name");
            if (!library.isDirectory() && !library.mkdirs()) throw new IOException("Cannot create sample folder");
            if (exists(library, filename) || exists(project, filename)) throw new IOException("Sample already exists");
            temporary = File.createTempFile(".import-", ".wav", library);
            try (FileOutputStream out = new FileOutputStream(temporary)) {
                byte[] header = new byte[12];
                int offset = 0;
                while (offset < header.length) {
                    int count = in.read(header, offset, header.length-offset);
                    if (count < 0) throw new IOException("Choose a RIFF/WAVE file");
                    offset += count;
                }
                if ( !new String(header,0,4,StandardCharsets.US_ASCII).equals("RIFF")
                        || !new String(header,8,4,StandardCharsets.US_ASCII).equals("WAVE"))
                    throw new IOException("Choose a RIFF/WAVE file");
                out.write(header); byte[] buffer = new byte[65536]; int count;
                while ((count=in.read(buffer)) != -1) out.write(buffer, 0, count);
                out.getFD().sync();
            }
            if (exists(library, filename) || exists(project, filename)) throw new IOException("Sample already exists");
            // Publish only the complete synced WAV, with no-overwrite semantics.
            Files.createLink(new File(library, filename).toPath(), temporary.toPath());
        } finally { if (temporary != null) temporary.delete(); }
    }
}
