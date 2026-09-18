package org.nullperator.app;

import java.io.*;
import java.nio.file.Files;

/** Stable, relative document IDs. Never expose private staging files or symlinks. */
final class DocumentFiles {
    static final String ROOT = "root";
    private final File root;
    DocumentFiles(File root) throws IOException { this.root = root.getCanonicalFile(); }
    File resolve(String id) throws FileNotFoundException {
        if (ROOT.equals(id)) return root;
        if (id == null || !id.startsWith(ROOT + "/")) throw new FileNotFoundException("Unknown document");
        File file = root;
        for (String part : id.substring(5).split("/", -1)) {
            try { validateName(part); } catch (IOException error) { throw new FileNotFoundException(error.getMessage()); }
            file = new File(file, part);
            if (Files.isSymbolicLink(file.toPath())) throw new FileNotFoundException("Links are not supported");
        }
        return file;
    }
    String id(File file) throws IOException {
        String relative = root.toPath().relativize(file.getAbsoluteFile().toPath()).toString();
        String id = relative.isEmpty() ? ROOT : ROOT + "/" + relative;
        resolve(id);
        return id;
    }
    static void validateName(String name) throws IOException {
        if (name == null || name.trim().isEmpty() || name.startsWith(".") || name.length() > 255
                || name.matches(".*[/\\\\:\\p{Cntrl}].*")) throw new IOException("Invalid file name");
    }
    File create(String parentId, String name, boolean directory) throws IOException {
        validateName(name);
        File parent = resolve(parentId);
        if (!parent.isDirectory()) throw new FileNotFoundException("Folder not found");
        if (SampleFiles.exists(parent, name)) throw new IOException("A file with this name already exists");
        File result = new File(parent, name);
        if (!(directory ? result.mkdir() : result.createNewFile())) throw new IOException("Could not create file");
        return result;
    }
    File rename(String id, String name) throws IOException {
        validateName(name);
        File file = resolve(id);
        if (file.equals(root)) throw new IOException("Cannot rename the root folder");
        if (file.getName().equals(name)) return file;
        if (SampleFiles.exists(file.getParentFile(), name)) throw new IOException("A file with this name already exists");
        File target = new File(file.getParentFile(), name);
        Files.move(file.toPath(), target.toPath());
        return target;
    }
    void delete(String id) throws IOException {
        if (ROOT.equals(id)) throw new IOException("Cannot delete the root folder");
        File file = resolve(id);
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot read folder");
            for (File child : children) delete(id(child));
        }
        Files.delete(file.toPath());
    }
}
