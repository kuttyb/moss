;;; moss-mode-tests.el --- ERT tests for moss-mode -*- lexical-binding: t; -*-

(require 'ert)
(require 'moss-mode)

(ert-deftest moss-mode-auto-mode-association ()
  (with-temp-buffer
    (setq buffer-file-name "/tmp/example.moss")
    (set-auto-mode)
    (should (eq major-mode 'moss-mode))))

(ert-deftest moss-mode-comment-and-string-syntax ()
  (with-temp-buffer
    (insert "value = \"# text\" # comment\n")
    (moss-mode)
    (syntax-propertize (point-max))
    (goto-char (point-min))
    (search-forward "# text")
    (should (nth 3 (syntax-ppss)))
    (search-forward "# comment")
    (should (nth 4 (syntax-ppss)))))

(ert-deftest moss-mode-two-space-indentation-and-else-dedent ()
  (with-temp-buffer
    (insert "fn choose(flag):\nif flag:\nreturn 1\nelse:\nreturn 2\n")
    (moss-mode)
    (indent-region (point-min) (point-max))
    (should
     (equal (buffer-string)
            "fn choose(flag):\n  if flag:\n    return 1\n  else:\n    return 2\n"))
    (should-not indent-tabs-mode)
    (should (= tab-width 2))))

(ert-deftest moss-mode-preserves-structural-dedents ()
  (with-temp-buffer
    (insert "fn first(flag):\n  if flag:\n    echo flag\n  echo false\n\nfn second():\n  return\n")
    (moss-mode)
    (indent-region (point-min) (point-max))
    (should
     (equal (buffer-string)
            "fn first(flag):\n  if flag:\n    echo flag\n  echo false\n\nfn second():\n  return\n"))))

(ert-deftest moss-mode-font-lock-basics ()
  (with-temp-buffer
    (insert "domain Worker:\n  fn Score(xs: Vector):\n    return xs |> map(_ * 2) |> sum\n")
    (moss-mode)
    (font-lock-ensure)
    (goto-char (point-min))
    (search-forward "domain")
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-keyword-face))
    (search-forward "Worker")
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-type-face))
    (let ((case-fold-search nil))
      (search-forward "map"))
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-builtin-face))))

(ert-deftest moss-mode-imenu-classifies-members ()
  (with-temp-buffer
    (insert "fn helper(x):\n  return x\n\ntype Box:\n  value: Int\n  fn Read():\n    return value\n\ndomain Worker:\n  fn Ping() -> Int:\n    reply 1\n  on Stop():\n    return\n")
    (moss-mode)
    (let ((index (moss-imenu-create-index)))
      (should (assoc "helper" (cdr (assoc "Functions" index))))
      (should (assoc "Box" (cdr (assoc "Types and Traits" index))))
      (should (assoc "Worker" (cdr (assoc "Domains" index))))
      (should (assoc "Read" (cdr (assoc "Methods" index))))
      (should (assoc "Ping" (cdr (assoc "Handlers" index))))
      (should (assoc "Stop" (cdr (assoc "Handlers" index)))))))

(ert-deftest moss-mode-command-construction-uses-source-diagnostics ()
  (let ((moss-compiler-command "/opt/moss compiler")
        (source "/tmp/a program.moss"))
    (should
     (equal "/opt/moss\\ compiler --diagnostic-paths --check /tmp/a\\ program.moss"
            (moss--check-command source)))))

(ert-deftest moss-mode-native-command-records-all-shared-artifacts ()
  (let ((moss-compiler-command "/opt/moss")
        (moss-rustc-command "rustc")
        (source (expand-file-name "tests/phase5_tooling.moss"
                                  default-directory)))
    (let ((command (moss--native-command source)))
      (should (string-match-p "--diagnostic-paths" command))
      (should (string-match-p "--native-output" command))
      (should (string-match-p "phase5_tooling\\.rs" command))
      (should (string-match-p
               (regexp-quote "rustc -D warnings -g -C debuginfo\\=2")
               command)))))

(ert-deftest moss-mode-debug-command-is-project-independent ()
  (let ((moss-compiler-command "/opt/moss")
        (moss-rustc-command "rustc")
        (source (expand-file-name "tests/phase5_tooling.moss"
                                  default-directory)))
    (let ((command (moss--debug-build-command source)))
      (should (string-match-p "/opt/moss --debug --diagnostic-paths" command))
      (should (string-match-p "--emit-debug-map" command))
      (should (string-match-p
               (regexp-quote "force-frame-pointers\\=yes") command))
      (should (string-match-p "rustc -D warnings" command)))))

(ert-deftest moss-mode-artifacts-are-derived-not-guessed-by-user ()
  (let ((default-directory (moss--repo-root default-directory)))
    (let ((artifacts (moss--artifact-alist
                      (expand-file-name "examples/counter.moss"
                                        default-directory))))
      (should (string-suffix-p "/build/emacs/counter/counter.rs"
                               (alist-get 'rust artifacts)))
      (should (string-suffix-p "/build/emacs/counter/counter.mossmap"
                               (alist-get 'map artifacts)))
      (should (string-suffix-p "/build/emacs/counter/counter"
                               (alist-get 'executable artifacts))))))

(ert-deftest moss-mode-compilation-diagnostic-is-clickable ()
  (let ((source (make-temp-file "moss-mode-" nil ".moss")))
    (unwind-protect
        (with-temp-buffer
          (compilation-mode)
          (let ((inhibit-read-only t))
            (insert source ":7: error: example Moss diagnostic\n"))
          (compilation--flush-parse (point-min) (point-max))
          (compilation--ensure-parse (point-max))
          (goto-char (point-min))
          (search-forward source)
          (should (get-text-property (1- (point)) 'compilation-message)))
      (delete-file source))))

(ert-deftest moss-mode-selects-symbol-specific-objdump-options ()
  (should
   (equal (moss--disassembly-arguments
           "/usr/bin/llvm-objdump" "moss__function__f" "/tmp/program")
          '("--demangle" "--source" "--line-numbers"
            "--disassemble-symbols=moss__function__f" "/tmp/program")))
  (should
   (equal (moss--disassembly-arguments
           "/usr/bin/objdump" "moss__function__f" "/tmp/program")
          '("--demangle" "--source" "--line-numbers"
            "--disassemble=moss__function__f" "/tmp/program"))))

(ert-deftest moss-mode-prefers-exact-reverse-source-mappings ()
  (let* ((generated (make-temp-file "moss-generated-" nil ".rs"))
         (source (make-temp-file "moss-source-" nil ".moss"))
         (broad `((semantic_identity . "fn:test@3")
                  (source . ((file . ,source) (start_line . 3) (end_line . 9)))
                  (generated . ((file . ,generated)
                                (start_line . 20) (end_line . 30)))
                  (line_mappings . (((moss_line . 3)
                                     (generated_rust_line . 20))))))
         (exact `((semantic_identity . "fn:test@3:statement@8")
                  (source . ((file . ,source) (start_line . 8) (end_line . 8)))
                  (generated . ((file . ,generated)
                                (start_line . 24) (end_line . 24)))
                  (line_mappings . (((moss_line . 8)
                                     (generated_rust_line . 24))))))
         (document `((entries . (,broad ,exact)))))
    (unwind-protect
        (progn
          (should (= 8 (nth 1 (moss--generated-origin
                               document generated 24))))
          (should (= 8 (nth 1 (moss--generated-origin
                               document generated 24 t))))
          (should-not (moss--generated-origin document generated 25 t)))
      (delete-file generated)
      (delete-file source))))

(provide 'moss-mode-tests)

;;; moss-mode-tests.el ends here
