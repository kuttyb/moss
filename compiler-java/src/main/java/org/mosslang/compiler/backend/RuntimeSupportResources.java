package org.mosslang.compiler.backend;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;

final class RuntimeSupportResources {
    private RuntimeSupportResources() {}

    static String read(String name) {
        String path = "/org/mosslang/compiler/backend/" + name;
        try (InputStream input = RuntimeSupportResources.class.getResourceAsStream(path)) {
            if (input == null) throw new IllegalStateException("missing bundled runtime resource " + path);
            return new String(input.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException error) {
            throw new UncheckedIOException("cannot read bundled runtime resource " + path, error);
        }
    }
}
