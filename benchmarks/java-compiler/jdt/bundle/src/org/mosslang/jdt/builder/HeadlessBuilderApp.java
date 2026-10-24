package org.mosslang.jdt.builder;

import org.eclipse.core.resources.*;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.Path;
import org.eclipse.equinox.app.IApplication;
import org.eclipse.equinox.app.IApplicationContext;
import org.eclipse.jdt.core.*;

import java.io.*;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class HeadlessBuilderApp implements IApplication {
    private static final String PROJECT_NAME = "moss-corpus";
    private final AtomicBoolean running = new AtomicBoolean(true);

    @Override
    public Object start(IApplicationContext context) throws Exception {
        String[] args = (String[]) context.getArguments().get("application.args");
        int port = 0;
        String action = null;
        String srcPath = null;
        String libPath = null;
        String binPath = null;

        for (int i = 0; i < args.length; i++) {
            if ("-port".equals(args[i]) && i + 1 < args.length) {
                port = Integer.parseInt(args[++i]);
            } else if ("-action".equals(args[i]) && i + 1 < args.length) {
                action = args[++i];
            } else if ("-src".equals(args[i]) && i + 1 < args.length) {
                srcPath = args[++i];
            } else if ("-lib".equals(args[i]) && i + 1 < args.length) {
                libPath = args[++i];
            } else if ("-bin".equals(args[i]) && i + 1 < args.length) {
                binPath = args[++i];
            }
        }

        if (port > 0) {
            runDaemon(port);
        } else if ("init".equalsIgnoreCase(action)) {
            initProject(srcPath, libPath, binPath);
            System.out.println("PROJECT_INITIALIZED");
            System.exit(0);
        } else if ("build".equalsIgnoreCase(action)) {
            BuildResult res = performBuild(false);
            System.out.println(res.toProtocolString());
            System.exit(0);
        } else if ("clean_build".equalsIgnoreCase(action)) {
            BuildResult res = performBuild(true);
            System.out.println(res.toProtocolString());
            System.exit(0);
        } else {
            System.err.println("Usage: -port <port> OR -action <init|build|clean_build> -src <src> -lib <lib> -bin <bin>");
            System.exit(1);
        }

        return IApplication.EXIT_OK;
    }

    @Override
    public void stop() {
        running.set(false);
    }

    private void runDaemon(int port) throws IOException {
        try (ServerSocket server = new ServerSocket(port, 50, InetAddress.getByName("127.0.0.1"))) {
            System.out.println("JDT_DAEMON_READY:" + server.getLocalPort());
            System.out.flush();

            while (running.get()) {
                try {
                    Socket client = server.accept();
                    handleClient(client);
                } catch (Exception e) {
                    if (!running.get()) break;
                    System.err.println("Daemon error: " + e.getMessage());
                }
            }
        }
    }

    private void handleClient(Socket socket) {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
             PrintWriter writer = new PrintWriter(new OutputStreamWriter(socket.getOutputStream()), true)) {
            
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty()) continue;
                String[] parts = line.split("\\s+");
                String cmd = parts[0].toUpperCase();

                if ("INIT".equals(cmd) && parts.length >= 4) {
                    initProject(parts[1], parts[2], parts[3]);
                    writer.println("OK INIT");
                } else if ("BUILD_FULL".equals(cmd)) {
                    BuildResult res = performBuild(true);
                    writer.println(res.toProtocolString());
                } else if ("BUILD_INCREMENTAL".equals(cmd)) {
                    BuildResult res = performBuild(false);
                    writer.println(res.toProtocolString());
                } else if ("REFRESH".equals(cmd)) {
                    long t0 = System.nanoTime();
                    IProject p = ResourcesPlugin.getWorkspace().getRoot().getProject(PROJECT_NAME);
                    if (p.exists()) p.refreshLocal(IResource.DEPTH_INFINITE, null);
                    long elapsed = (System.nanoTime() - t0) / 1_000_000;
                    writer.println("OK REFRESH " + elapsed + " ms");
                } else if ("STOP".equals(cmd)) {
                    writer.println("OK STOPPING");
                    running.set(false);
                    new Thread(() -> {
                        try { Thread.sleep(200); System.exit(0); } catch (Exception ignored) {}
                    }).start();
                    break;
                } else {
                    writer.println("ERROR Unknown command: " + line);
                }
            }
        } catch (Exception e) {
            System.err.println("Client handler error: " + e.getMessage());
        }
    }

    public synchronized void initProject(String srcDir, String libDir, String binDir) throws CoreException {
        IWorkspaceRoot root = ResourcesPlugin.getWorkspace().getRoot();
        IProject project = root.getProject(PROJECT_NAME);

        if (!project.exists()) {
            project.create(null);
        }
        if (!project.isOpen()) {
            project.open(null);
        }

        IProjectDescription desc = project.getDescription();
        desc.setNatureIds(new String[]{JavaCore.NATURE_ID});
        
        ICommand command = desc.newCommand();
        command.setBuilderName(JavaCore.BUILDER_ID);
        desc.setBuildSpec(new ICommand[]{command});
        project.setDescription(desc, null);

        IJavaProject javaProject = JavaCore.create(project);

        IFolder srcFolder = project.getFolder("src");
        if (srcFolder.exists()) {
            srcFolder.delete(true, null);
        }
        srcFolder.createLink(new Path(new File(srcDir).getAbsolutePath()), IResource.ALLOW_MISSING_LOCAL | IResource.REPLACE, null);

        IFolder binFolder = project.getFolder("bin");
        if (binFolder.exists()) {
            binFolder.delete(true, null);
        }
        binFolder.createLink(new Path(new File(binDir).getAbsolutePath()), IResource.ALLOW_MISSING_LOCAL | IResource.REPLACE, null);

        Map<String, String> options = javaProject.getOptions(false);
        JavaCore.setComplianceOptions(JavaCore.VERSION_25, options);
        options.put(JavaCore.COMPILER_SOURCE, JavaCore.VERSION_25);
        options.put(JavaCore.COMPILER_CODEGEN_TARGET_PLATFORM, JavaCore.VERSION_25);
        options.put(JavaCore.COMPILER_COMPLIANCE, JavaCore.VERSION_25);
        javaProject.setOptions(options);

        List<IClasspathEntry> entries = new ArrayList<>();
        entries.add(JavaCore.newSourceEntry(srcFolder.getFullPath()));

        File libDirectory = new File(libDir);
        File[] jars = libDirectory.listFiles((d, name) -> name.endsWith(".jar"));
        if (jars != null) {
            for (File jar : jars) {
                entries.add(JavaCore.newLibraryEntry(new Path(jar.getAbsolutePath()), null, null));
            }
        }

        // Add standard JRE container for JavaSE-25
        entries.add(JavaCore.newContainerEntry(new Path("org.eclipse.jdt.launching.JRE_CONTAINER/org.eclipse.jdt.internal.debug.ui.launcher.StandardVMType/JavaSE-25")));

        javaProject.setRawClasspath(entries.toArray(new IClasspathEntry[0]), binFolder.getFullPath(), null);
        project.refreshLocal(IResource.DEPTH_INFINITE, null);
    }

    public synchronized BuildResult performBuild(boolean full) throws CoreException {
        IWorkspaceRoot root = ResourcesPlugin.getWorkspace().getRoot();
        IProject project = root.getProject(PROJECT_NAME);
        if (!project.exists() || !project.isOpen()) {
            return new BuildResult(false, 0, 0, 1, 0, "Project not open");
        }

        long tStart = System.nanoTime();
        project.refreshLocal(IResource.DEPTH_INFINITE, null);
        long tAfterRefresh = System.nanoTime();

        AtomicInteger changedClasses = new AtomicInteger(0);
        IResourceChangeListener listener = event -> {
            if (event.getDelta() != null) {
                try {
                    event.getDelta().accept(delta -> {
                        IResource res = delta.getResource();
                        if (res.getType() == IResource.FILE && "class".equals(res.getFileExtension())) {
                            int k = delta.getKind();
                            if (k == IResourceDelta.ADDED || k == IResourceDelta.CHANGED) {
                                changedClasses.incrementAndGet();
                            }
                        }
                        return true;
                    });
                } catch (CoreException ignored) {}
            }
        };

        IWorkspace ws = ResourcesPlugin.getWorkspace();
        ws.addResourceChangeListener(listener, IResourceChangeEvent.POST_BUILD);

        try {
            int kind = full ? IncrementalProjectBuilder.FULL_BUILD : IncrementalProjectBuilder.INCREMENTAL_BUILD;
            project.build(kind, null);
        } finally {
            ws.removeResourceChangeListener(listener);
        }

        long tEnd = System.nanoTime();
        long wallTimeMs = (tEnd - tStart) / 1_000_000;
        long compileTimeMs = (tEnd - tAfterRefresh) / 1_000_000;

        IMarker[] markers = project.findMarkers(IJavaModelMarker.JAVA_MODEL_PROBLEM_MARKER, true, IResource.DEPTH_INFINITE);
        int errors = 0;
        int warnings = 0;
        StringBuilder diag = new StringBuilder();
        for (IMarker m : markers) {
            int severity = m.getAttribute(IMarker.SEVERITY, -1);
            if (severity == IMarker.SEVERITY_ERROR) {
                errors++;
                if (errors <= 5) {
                    diag.append(m.getAttribute(IMarker.MESSAGE, "")).append("; ");
                }
            } else if (severity == IMarker.SEVERITY_WARNING) {
                warnings++;
            }
        }

        return new BuildResult(errors == 0, wallTimeMs, compileTimeMs, errors, warnings, changedClasses.get(), diag.toString());
    }

    public record BuildResult(
        boolean success,
        long wallTimeMs,
        long compileTimeMs,
        int errors,
        int warnings,
        int changedClasses,
        String diagnosticsSummary
    ) {
        public BuildResult(boolean s, long w, long c, int e, int warn, String diag) {
            this(s, w, c, e, warn, 0, diag);
        }

        public String toProtocolString() {
            return String.format("%s wall_ms=%d compile_ms=%d changed_classes=%d errors=%d warnings=%d diag=%s",
                success ? "SUCCESS" : "FAILED", wallTimeMs, compileTimeMs, changedClasses, errors, warnings,
                diagnosticsSummary.isEmpty() ? "none" : diagnosticsSummary);
        }
    }
}
