;;; moss-mode-tests.el --- ERT tests for moss-mode -*- lexical-binding: t; -*-

(require 'ert)
(require 'moss-mode)

(defun moss-test--with-domain-navigation (function)
  "Call FUNCTION in the real-compiler domain-navigation fixture buffer."
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_domain_navigation.moss"
                  root))
         (moss-compiler-command (expand-file-name "moss" root))
         (source-buffer (find-file-noselect source)))
    (unwind-protect
        (with-current-buffer source-buffer
          (moss-mode)
          (funcall function source))
      (when (get-buffer "*Moss Call Tree*")
        (kill-buffer "*Moss Call Tree*"))
      (when (buffer-live-p source-buffer)
        (kill-buffer source-buffer)))))

(defun moss-test--goto-line-token (line token)
  "Move point to TOKEN on one-based LINE in the current buffer."
  (goto-char (point-min))
  (forward-line (1- line))
  (let ((case-fold-search nil))
    (search-forward token (line-end-position)))
  (backward-char (length token)))

(defun moss-test--capture-xrefs (command)
  "Invoke interactive COMMAND and return the xrefs it displays."
  (let* ((shown nil)
         (xref-show-xrefs-function
          (lambda (fetcher _display-action)
            (setq shown (funcall fetcher)))))
    (funcall command)
    shown))

(defun moss-test--write-debug-map (filename source generated exact-line)
  "Write a minimal valid map to FILENAME for SOURCE and GENERATED."
  (let* ((mappings
          (vector `((moss_line . ,exact-line)
                    (generated_rust_line . 11))))
         (entry
          `((semantic_identity . "main@1")
            (construct_kind . "main")
            (source . ((file . ,source)
                       (start_line . 1) (end_line . 3)))
            (generated . ((file . ,generated)
                          (start_line . 10) (end_line . 12)))
            (line_mappings . ,mappings)))
         (document
          `((format . "moss-debug-map")
            (version . 1)
            (entries . ,(vector entry)))))
    (with-temp-file filename
      (insert (json-encode document)))))

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

(ert-deftest moss-mode-phase7-declarations ()
  (with-temp-buffer
    (insert "test \"addition\":\n  assert(true)\n\nbench \"addition\":\n  add(2, 3)\n")
    (moss-mode)
    (font-lock-ensure)
    (goto-char (point-min))
    (search-forward "test")
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-keyword-face))
    (search-forward "addition")
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-function-name-face))
    (search-forward "assert")
    (should (eq (get-text-property (1- (point)) 'face)
                'font-lock-builtin-face))
    (let ((index (moss-imenu-create-index)))
      (should (assoc "addition" (cdr (assoc "Tests" index))))
      (should (assoc "addition" (cdr (assoc "Benchmarks" index)))))))

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

(ert-deftest moss-mode-imenu-indexes-exported-project-declarations ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "examples/projects/ledger/src/accounts.moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (let ((index (moss-imenu-create-index)))
              (should (assoc "accounts" (cdr (assoc "Modules" index))))
              (should (assoc "credit" (cdr (assoc "Functions" index))))
              (should (assoc "Ledger" (cdr (assoc "Domains" index))))
              (should (assoc "Journal" (cdr (assoc "Domains" index))))
              (dolist (handler
                       '("Credit" "Balance" "Record" "Count" "Last"))
                (should
                 (assoc handler (cdr (assoc "Handlers" index)))))))
        (kill-buffer (current-buffer))))))

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

(ert-deftest moss-mode-finds-versioned-debian-lldb-dap ()
  (cl-letf (((symbol-function 'file-directory-p) (lambda (_) t))
            ((symbol-function 'directory-files)
             (lambda (&rest _)
               '("/usr/bin/lldb-dap-18" "/usr/bin/lldb-dap-19")))
            ((symbol-function 'file-executable-p) (lambda (_) t)))
    (should (equal "/usr/bin/lldb-dap-19"
                   (moss--versioned-executable "lldb-dap")))))

(ert-deftest moss-mode-reports-nonexecutable-lldb-dap ()
  (let ((moss-lldb-dap-command "/tmp/moss-nonexecutable-lldb-dap"))
    (cl-letf (((symbol-function 'file-executable-p) (lambda (_) nil)))
      (let ((error-data (should-error (moss--lldb-dap) :type 'user-error)))
        (should
         (string-match-p "lldb-dap is not executable"
                         (error-message-string error-data)))))))

(ert-deftest moss-mode-debug-emits-validated-dape-configuration ()
  (let* ((source (make-temp-file "moss-dape-" nil ".moss"))
         (generated (make-temp-file "moss-dape-" nil ".rs"))
         (map-file (make-temp-file "moss-dape-" nil ".mossmap"))
         (executable (make-temp-file "moss-dape-program-"))
         (script (make-temp-file "moss-dape-helper-" nil ".py"))
         (adapter "/usr/bin/lldb-dap-19")
         (root (file-name-directory source))
         (artifacts `((executable . ,executable)
                      (map . ,map-file)
                      (root . ,root)))
         captured-config)
    (unwind-protect
        (progn
          (with-temp-file source
            (insert "fn main():\n  echo 1\n"))
          (set-file-modes executable #o700)
          (moss-test--write-debug-map map-file source generated 2)
          (with-current-buffer (find-file-noselect source)
            (moss-mode)
            (goto-char (point-min))
            (forward-line 1)
            (setq moss--breakpoint-overlays
                  (list (make-overlay (line-beginning-position)
                                      (line-beginning-position 2))))
            (cl-letf (((symbol-function 'require)
                       (lambda (feature &optional _filename _noerror)
                         (eq feature 'dape)))
                      ((symbol-function 'moss--artifact-alist)
                         (lambda (_source) artifacts))
                        ((symbol-function 'moss--lldb-script)
                         (lambda () script))
                        ((symbol-function 'moss--lldb-dap)
                         (lambda () adapter))
                        ((symbol-function 'add-hook)
                         (lambda (&rest _arguments) nil))
                        ((symbol-function 'dape)
                         (lambda (config &optional _skip-compile)
                           (setq captured-config config))))
              (moss-debug)))
          (should (equal adapter (plist-get captured-config 'command)))
          (should (equal root (plist-get captured-config 'command-cwd)))
          (should (equal "lldb-dap" (plist-get captured-config :type)))
          (should (equal "launch" (plist-get captured-config :request)))
          (should (equal executable (plist-get captured-config :program)))
          (should (equal root (plist-get captured-config :cwd)))
          (should
           (equal
            (vector (format "command script import %S" script)
                    (format "moss-map-load %S" map-file))
            (plist-get captured-config :initCommands)))
          (should
           (equal
            (vector (format "moss-break %S" (format "%s:2" source)))
            (plist-get captured-config :preRunCommands)))
          (should-not (plist-get captured-config :stopOnEntry)))
      (when-let ((buffer (get-file-buffer source)))
        (kill-buffer buffer))
      (mapc (lambda (filename)
              (when (file-exists-p filename) (delete-file filename)))
            (list source generated map-file executable script)))))

(ert-deftest moss-mode-debug-rejects-range-only-breakpoint ()
  (let* ((source (make-temp-file "moss-dape-inexact-" nil ".moss"))
         (generated (make-temp-file "moss-dape-inexact-" nil ".rs"))
         (map-file (make-temp-file "moss-dape-inexact-" nil ".mossmap"))
         (executable (make-temp-file "moss-dape-inexact-program-"))
         (script (make-temp-file "moss-dape-inexact-helper-" nil ".py"))
         (artifacts `((executable . ,executable)
                      (map . ,map-file)
                      (root . ,(file-name-directory source)))))
    (unwind-protect
        (progn
          (with-temp-file source
            (insert "fn main():\n\n  echo 1\n"))
          (set-file-modes executable #o700)
          (moss-test--write-debug-map map-file source generated 3)
          (with-current-buffer (find-file-noselect source)
            (moss-mode)
            (goto-char (point-min))
            (forward-line 1)
            (setq moss--breakpoint-overlays
                  (list (make-overlay (line-beginning-position)
                                      (line-beginning-position 2))))
            (cl-letf (((symbol-function 'require)
                       (lambda (feature &optional _filename _noerror)
                         (eq feature 'dape)))
                      ((symbol-function 'moss--artifact-alist)
                         (lambda (_source) artifacts))
                        ((symbol-function 'moss--lldb-script)
                         (lambda () script))
                        ((symbol-function 'moss--lldb-dap)
                         (lambda () "/usr/bin/lldb-dap-19")))
                (let ((error-data (should-error
                                   (moss-debug)
                                   :type 'user-error)))
                  (should
                   (string-match-p
                    "breakpoint has no exact generated mapping"
                    (error-message-string error-data)))))))
      (when-let ((buffer (get-file-buffer source)))
        (kill-buffer buffer))
      (mapc (lambda (filename)
              (when (file-exists-p filename) (delete-file filename)))
            (list source generated map-file executable script)))))

(ert-deftest moss-mode-debug-distinguishes-missing-artifacts ()
  (let* ((source (make-temp-file "moss-dape-missing-" nil ".moss"))
         (executable (concat source ".missing-executable"))
         (map-file (concat source ".missing-map"))
         (artifacts `((executable . ,executable)
                      (map . ,map-file)
                      (root . ,(file-name-directory source)))))
    (unwind-protect
        (progn
          (with-temp-file source (insert "fn main():\n  echo 1\n"))
          (with-current-buffer (find-file-noselect source)
            (moss-mode)
            (cl-letf (((symbol-function 'require)
                       (lambda (feature &optional _filename _noerror)
                         (eq feature 'dape)))
                      ((symbol-function 'moss--artifact-alist)
                       (lambda (_source) artifacts)))
              (let ((error-data (should-error (moss-debug) :type 'user-error)))
                (should
                 (string-match-p "debug executable not found"
                                 (error-message-string error-data))))
              (with-temp-file executable (insert "debug fixture"))
              (set-file-modes executable #o700)
              (let ((error-data (should-error (moss-debug) :type 'user-error)))
                (should
                 (string-match-p "debug map not found"
                                 (error-message-string error-data)))))))
      (when-let ((buffer (get-file-buffer source)))
        (kill-buffer buffer))
      (dolist (filename (list source executable map-file))
        (when (file-exists-p filename) (delete-file filename))))))

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

(ert-deftest moss-mode-semantic-cache-invalidates-after-change ()
  (with-temp-buffer
    (moss-mode)
    (setq moss--semantic-cache (make-hash-table :test #'equal))
    (puthash 'query '(1 . result) moss--semantic-cache)
    (insert "x")
    (should (= 0 (hash-table-count moss--semantic-cache)))))

(ert-deftest moss-mode-xref-definition-uses-compiler-location ()
  (with-temp-buffer
    (insert "inspect(sample)\n")
    (setq buffer-file-name "/tmp/example.moss")
    (moss-mode)
    (cl-letf (((symbol-function 'moss--references-at-point)
               (lambda ()
                 '((target . ((kind . "function") (name . "inspect")))
                   (definition . ((source . ((file . "/src/lib.moss")
                                             (line . 7) (column . 1)))))))))
      (let* ((xref (car (xref-backend-definitions 'moss "inspect")))
             (location (xref-item-location xref)))
        (should (equal "/src/lib.moss" (xref-file-location-file location)))
        (should (= 7 (xref-file-location-line location)))))))

(ert-deftest moss-mode-xref-references-excludes-declaration ()
  (with-temp-buffer
    (setq buffer-file-name "/tmp/example.moss")
    (moss-mode)
    (cl-letf (((symbol-function 'moss--references-at-point)
               (lambda ()
                 '((target . ((name . "work")))
                   (references . (((kind . "declaration")
                                   (source . ((file . "/src/a.moss") (line . 1))))
                                  ((kind . "call")
                                   (source . ((file . "/src/b.moss") (line . 9))))))))))
      (let ((xrefs (xref-backend-references 'moss "work")))
        (should (= 1 (length xrefs)))
        (should (equal "/src/b.moss"
                       (xref-file-location-file
                        (xref-item-location (car xrefs)))))))))

(ert-deftest moss-mode-xref-apropos-preserves-symbol-order ()
  (with-temp-buffer
    (setq buffer-file-name "/tmp/example.moss")
    (moss-mode)
    (cl-letf (((symbol-function 'moss--semantic-query)
               (lambda (&rest _)
                 '((symbols . (((qualified_name . "A") (kind . "type")
                                (source . ((file . "/a") (line . 1))))
                               ((qualified_name . "B") (kind . "function")
                                (source . ((file . "/b") (line . 2))))))))))
      (should (equal '("A — type" "B — function")
                     (mapcar #'xref-item-summary
                             (xref-backend-apropos 'moss "")))))))

(ert-deftest moss-mode-capf-exposes-semantic-annotations ()
  (with-temp-buffer
    (insert "tot")
    (setq buffer-file-name "/tmp/example.moss")
    (moss-mode)
    (cl-letf (((symbol-function 'moss--semantic-query)
               (lambda (&rest _)
                 '((candidates . (((label . "total") (kind . "function")
                                   (detail . "function (Int) -> Int"))))))))
      (let* ((capf (moss-completion-at-point))
             (table (nth 2 capf)))
        (should (member "total" table))
        (should (equal "  function (Int) -> Int"
                       (moss--completion-annotation "total")))))))

(ert-deftest moss-mode-call-tree-marks-repeated-semantic-identity ()
  (with-temp-buffer
    (moss-call-tree-mode)
    (let ((inhibit-read-only t))
      (moss--call-tree-insert-node "again" "entity-v1:function:f"
                                   "/f.moss" 2 1
                                   '("entity-v1:function:f")))
    (should (string-match-p "\\[repeated\\]" (buffer-string)))
    (should-not (get-text-property (point-min) 'moss-node-id))))

(ert-deftest moss-mode-callers-and-callees-use-compiler-call-records ()
  (with-temp-buffer
    (setq buffer-file-name "/tmp/example.moss")
    (moss-mode)
    (let (shown)
      (cl-letf (((symbol-function 'moss--calls-at-point)
                 (lambda ()
                   '((callers . (((source . "fn:outer")
                                  (source_file . "/src/a.moss") (line . 4))))
                     (direct_calls . (((target . "fn:inner")
                                       (boundary . "ordinary_call")
                                       (source_file . "/src/a.moss")
                                       (line . 8))
                                      ((target . "handler:Worker.Add")
                                       (boundary . "synchronous_message_by_value")
                                       (source_file . "/src/a.moss")
                                       (line . 9)))))))
                ((symbol-function 'moss--show-xrefs)
                 (lambda (xrefs) (setq shown xrefs))))
        (moss-callers)
        (should (= 1 (length shown)))
        (moss-callees)
        (should (= 2 (length shown)))
        (should (string-match-p "synchronous_message"
                                (xref-item-summary (cadr shown))))))))

(ert-deftest moss-mode-call-tree-expands-message-and-nested-edges ()
  (let ((source-buffer (generate-new-buffer " *moss-tree-source*")))
    (unwind-protect
        (with-temp-buffer
          (moss-call-tree-mode)
          (setq moss--call-tree-source-buffer source-buffer)
          (let ((inhibit-read-only t))
            (moss--call-tree-insert-node "root" "entity-v1:function:root"
                                         "/src/a.moss" 1 0 nil))
          (cl-letf (((symbol-function 'moss--semantic-query)
                     (lambda (&rest _)
                       '((direct_calls .
                          (((target . "handler:Worker.Add")
                            (target_id . "entity-v1:handler:Worker.Add")
                            (boundary . "synchronous_message_by_value")
                            (source_file . "/src/a.moss") (line . 6))))))))
            (goto-char (point-min))
            (moss-call-tree-toggle)
            (should (string-match-p "\\[message\\] handler:Worker.Add"
                                    (buffer-string)))
            (forward-line 1)
            (should (equal "entity-v1:handler:Worker.Add"
                           (get-text-property (point) 'moss-node-id)))))
      (kill-buffer source-buffer))))

(ert-deftest moss-mode-real-compiler-completes-incomplete-source ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_incomplete.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-max))
            (skip-chars-backward "\n")
            (let ((capf (moss-completion-at-point)))
              (should (member "total" (nth 2 capf)))))
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-domain-goto-definition ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((21 "Ledger" 2)
                     (9 "Ledger" 2)
                     (22 "Inventory" 8)
                     (23 "App" 14)))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (let* ((xref (car (xref-backend-definitions 'moss (nth 1 case))))
              (location (xref-item-location xref)))
         (should (= (nth 2 case) (xref-file-location-line location))))))))

(ert-deftest moss-mode-real-compiler-handler-goto-definition ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((12 "Read" 5)
                     (18 "Available" 11)
                     (24 "Run" 17)))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (let* ((xref (car (xref-backend-definitions 'moss (nth 1 case))))
              (location (xref-item-location xref)))
         (should (= (nth 2 case) (xref-file-location-line location))))))))

(ert-deftest moss-mode-real-compiler-domain-references ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (moss-test--goto-line-token 21 "Ledger")
     (should
      (equal '(9 15 21)
             (mapcar
              (lambda (xref)
                (xref-file-location-line (xref-item-location xref)))
              (xref-backend-references 'moss "Ledger")))))))

(ert-deftest moss-mode-real-compiler-handler-references ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((12 "Read") (18 "Available") (24 "Run")))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (let ((references (xref-backend-references 'moss (nth 1 case))))
         (should (= 1 (length references)))
         (should (= (nth 0 case)
                    (xref-file-location-line
                     (xref-item-location (car references))))))))))

(ert-deftest moss-mode-real-compiler-callers ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((5 "Read" "handler:Inventory.Available" 12)
                     (11 "Available" "handler:App.Run" 18)
                     (17 "Run" "main" 24)))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (let ((xrefs (moss-test--capture-xrefs #'moss-callers)))
         (should (= 1 (length xrefs)))
         (should (string-match-p (regexp-quote (nth 2 case))
                                 (xref-item-summary (car xrefs))))
         (should (= (nth 3 case)
                    (xref-file-location-line
                     (xref-item-location (car xrefs))))))))))

(ert-deftest moss-mode-real-compiler-callees ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((17 "Run" "handler:Inventory.Available" 11)
                     (11 "Available" "handler:Ledger.Read" 5)))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (let ((xrefs (moss-test--capture-xrefs #'moss-callees)))
         (should (= 1 (length xrefs)))
         (should (string-match-p (regexp-quote (nth 2 case))
                                 (xref-item-summary (car xrefs))))
         (should (= (nth 3 case)
                    (xref-file-location-line
                     (xref-item-location (car xrefs)))))))
     (moss-test--goto-line-token 5 "Read")
     (should-not (moss-test--capture-xrefs #'moss-callees)))))

(ert-deftest moss-mode-real-compiler-call-tree-command ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (dolist (case '((12 "Read" "handler Ledger.Read")
                     (5 "Read" "handler Ledger.Read")
                     (11 "Available" "handler Inventory.Available")
                     (17 "Run" "handler App.Run")))
       (moss-test--goto-line-token (nth 0 case) (nth 1 case))
       (cl-letf (((symbol-function 'pop-to-buffer)
                  (lambda (buffer &rest _ignore) buffer)))
         (moss-call-tree))
       (with-current-buffer "*Moss Call Tree*"
         (should (string-match-p (regexp-quote (nth 2 case))
                                 (buffer-string))))))))

(ert-deftest moss-mode-real-compiler-call-tree-expansion ()
  (moss-test--with-domain-navigation
   (lambda (_source)
     (moss-test--goto-line-token 17 "Run")
     (cl-letf (((symbol-function 'pop-to-buffer)
                (lambda (buffer &rest _ignore) buffer)))
       (moss-call-tree))
     (with-current-buffer "*Moss Call Tree*"
       (goto-char (point-min))
       (moss-call-tree-toggle)
       (should (string-match-p
                "\\[message\\] handler:Inventory.Available"
                (buffer-string)))
       (forward-line 1)
       (should (= 11 (get-text-property (point) 'moss-node-line)))
       (moss-call-tree-toggle)
       (should (string-match-p "\\[message\\] handler:Ledger.Read"
                               (buffer-string)))
       (forward-line 1)
       (should (= 5 (get-text-property (point) 'moss-node-line)))))))

(ert-deftest moss-mode-real-compiler-call-tree-visit ()
  (moss-test--with-domain-navigation
   (lambda (source)
     (moss-test--goto-line-token 17 "Run")
     (cl-letf (((symbol-function 'pop-to-buffer)
                (lambda (buffer &rest _ignore) buffer)))
       (moss-call-tree))
     (with-current-buffer "*Moss Call Tree*"
       (goto-char (point-min))
       (moss-call-tree-toggle)
       (forward-line 1)
       (cl-letf (((symbol-function 'find-file-other-window)
                  (lambda (file)
                    (should (equal source file))
                    (set-buffer (find-file-noselect file)))))
         (moss-call-tree-visit)
         (should (= 11 (line-number-at-pos))))))))

(ert-deftest moss-mode-real-compiler-xref-cross-file-and-semantic-references ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_ide/src/main.moss" root))
         (library (expand-file-name
                   "tests/tooling/fixtures/phase225_ide/src/library.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-min))
            (search-forward "leaf")
            (let* ((definition (car (xref-backend-definitions 'moss "leaf")))
                   (location (xref-item-location definition))
                   (references (xref-backend-references 'moss "leaf")))
              (should (equal library (xref-file-location-file location)))
              (should (= 11 (xref-file-location-line location)))
              (should (= 3 (length references)))))
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-examples-module-and-type-navigation ()
  (let* ((root (moss--repo-root default-directory))
         (compiler (expand-file-name "moss" root))
         (phase10-main (expand-file-name
                        "examples/projects/phase10_modules/src/main.moss"
                        root))
         (phase10-first (expand-file-name
                         "examples/projects/phase10_modules/src/first.moss"
                         root))
         (ledger-main (expand-file-name
                       "examples/projects/ledger/src/main.moss" root))
         (ledger-model (expand-file-name
                        "examples/projects/ledger/src/model.moss" root))
         (ledger-service (expand-file-name
                          "examples/projects/ledger/src/service.moss" root))
         (moss-compiler-command compiler))
    (dolist (case `((,phase10-main 2 "First" ,phase10-first 1)
                    (,phase10-main 5 "add_bonus" ,phase10-first 6)
                    (,ledger-main 2 "model" ,ledger-model 1)
                    (,ledger-service 7 "Order" ,ledger-model 7)))
      (with-current-buffer (find-file-noselect (nth 0 case))
        (unwind-protect
            (progn
              (moss-mode)
              (moss-test--goto-line-token (nth 1 case) (nth 2 case))
              (let* ((xref (car (xref-backend-definitions
                                 'moss (nth 2 case))))
                     (location (xref-item-location xref)))
                (should (equal (nth 3 case)
                               (xref-file-location-file location)))
                (should (= (nth 4 case)
                           (xref-file-location-line location)))))
          (kill-buffer (current-buffer)))))))

(ert-deftest moss-mode-real-examples-domain-calls-and-test-context ()
  (let* ((root (moss--repo-root default-directory))
         (compiler (expand-file-name "moss" root))
         (ledger-main (expand-file-name
                       "examples/projects/ledger/src/main.moss" root))
         (ledger-service (expand-file-name
                          "examples/projects/ledger/src/service.moss" root))
         (phase7-test (expand-file-name
                       "examples/projects/phase7_demo/tests/arithmetic.moss"
                       root))
         (moss-compiler-command compiler))
    (with-current-buffer (find-file-noselect ledger-main)
      (unwind-protect
          (progn
            (moss-mode)
            (moss-test--goto-line-token 12 "Submit")
            (let* ((definition (car (xref-backend-definitions 'moss "Submit")))
                   (location (xref-item-location definition))
                   (references (xref-backend-references 'moss "Submit")))
              (should (equal ledger-service
                             (xref-file-location-file location)))
              (should (= 7 (xref-file-location-line location)))
              (should (equal '(12 13)
                             (mapcar
                              (lambda (xref)
                                (xref-file-location-line
                                 (xref-item-location xref)))
                              references))))
            (let ((callees (moss-test--capture-xrefs #'moss-callees)))
              (should (= 3 (length callees)))))
        (kill-buffer (current-buffer))))
    (with-current-buffer (find-file-noselect phase7-test)
      (unwind-protect
          (progn
            (moss-mode)
            (let ((symbols
                   (mapcar
                    (lambda (item) (moss--json-get 'qualified_name item))
                    (moss--json-get
                     'symbols (moss--semantic-query "symbols" nil)))))
              (should (member "add" symbols))
              (should (member "doubled" symbols))
              (should (member "external arithmetic" symbols))))
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-method-and-message-references ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_ide/src/main.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-min))
            (let ((case-fold-search nil))
              (search-forward "Read"))
            (should (= 2 (length (xref-backend-references 'moss "Read"))))
            (search-forward "top")
            (let ((location (xref-item-location
                             (car (xref-backend-definitions 'moss "top")))))
              (should (equal source (xref-file-location-file location)))
              (should (= 1 (xref-file-location-line location))))
            (search-forward "Add")
            (let ((references (xref-backend-references 'moss "Add")))
              (should (= 1 (length references)))
              (should (string-match-p "handler_message"
                                      (xref-item-summary (car references))))))
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-ambiguity-is-not-guessed ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_shadow.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (should-error
             (moss--semantic-query "references" "binding:value")
             :type 'moss-semantic-error))
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-workspace-symbol-kinds-and-modules ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_ide/src/main.moss" root))
         (module-source (expand-file-name
                         "tests/tooling/fixtures/phase225_modules/src/main.moss"
                         root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (dolist (file (list source module-source))
      (with-current-buffer (find-file-noselect file)
        (unwind-protect
            (progn
              (moss-mode)
              (let* ((first (moss--json-get
                             'symbols (moss--semantic-query "symbols" nil)))
                     (second (moss--json-get
                              'symbols (moss--semantic-query "symbols" nil)))
                     (projection
                      (mapcar (lambda (item)
                                (cons (moss--json-get 'qualified_name item)
                                      (moss--json-get 'kind item))) first)))
                (should (equal first second))
                (if (equal file source)
                    (progn
                      (should (member '("top" . "function") projection))
                      (should (member '("Counter" . "type") projection))
                      (should (member '("Worker" . "domain") projection))
                      (should (member '("Worker.Add" . "handler") projection)))
                  (should (member '("math.leaf" . "function") projection)))))
          (kill-buffer (current-buffer)))))))

(ert-deftest moss-mode-real-compiler-contextual-completion-kinds ()
  (let* ((root (moss--repo-root default-directory))
         (compiler (expand-file-name "moss" root))
         (cases `(("tests/tooling/fixtures/phase225_ide/src/main.moss"
                   "at:8:24" "Read" "method")
                  ("tests/tooling/fixtures/phase225_ide/src/main.moss"
                   "at:11:27" "Add" "handler")
                  ("tests/tooling/fixtures/phase225_type_incomplete.moss"
                   "at:4:21" "Counter" "type")
                  ("tests/tooling/fixtures/phase225_modules/src/main.moss"
                   "at:5:13" "leaf" "function"))))
    (dolist (case cases)
      (let ((file (expand-file-name (nth 0 case) root))
            (moss-compiler-command compiler))
        (with-current-buffer (find-file-noselect file)
          (unwind-protect
              (progn
                (moss-mode)
                (let ((candidates
                     (moss--json-get
                      'candidates
                      (moss--semantic-query "complete" (nth 1 case) nil t))))
                  (should (cl-some
                           (lambda (candidate)
                             (and (equal (moss--json-get 'label candidate)
                                         (nth 2 case))
                                  (equal (moss--json-get 'kind candidate)
                                         (nth 3 case))))
                           candidates))))
            (kill-buffer (current-buffer))))))))

(ert-deftest moss-mode-real-compiler-filters-pipeline-capf-by-legality ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_pipeline_completion.moss"
                  root))
         (moss-compiler-command (expand-file-name "moss" root))
         (all-operations
          '("all" "any" "count" "filter" "map" "reduce" "sum")))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (let ((original (buffer-string)))
            (moss-mode)
            (cl-labels
                ((complete-line
                  (line expression)
                  (let ((inhibit-read-only t))
                    (erase-buffer)
                    (insert original)
                    (goto-char (point-min))
                    (forward-line (1- line))
                    (delete-region (line-beginning-position)
                                   (line-end-position))
                    (insert "  echo " expression " |> ")
                    (sort (copy-sequence
                           (or (nth 2 (moss-completion-at-point)) '()))
                          #'string-lessp))))
              (should (equal all-operations
                             (complete-line 16 "values")))
              (let ((after-map
                     (complete-line 16 "values |> map(to_string)")))
                (should (equal '("all" "any" "count" "map" "reduce")
                               after-map)))
              (should (equal all-operations
                             (complete-line
                              16 "values |> filter(keep_int)")))
              (should-not (complete-line 16 "values |> sum"))
              (let ((items (complete-line 17 "items")))
                (should (member "count" items))
                (should-not (member "sum" items)))))
        (set-buffer-modified-p nil)
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-semantic-cache-avoids-process-and-invalidates-on-edit ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_shadow.moss" root))
         (moss-semantic-cache-seconds 60)
         (invocations 0)
         argument-lists)
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (cl-letf (((symbol-function 'moss--compiler)
                       (lambda (&rest _) "/mock/moss"))
                      ((symbol-function 'process-file)
                       (lambda (_program _infile _destination _display
                                &rest arguments)
                         (setq invocations (1+ invocations))
                         (push arguments argument-lists)
                         (insert "{\"ok\":true,\"result\":{\"symbols\":[]}}")
                         0)))
              (moss--semantic-query "symbols" nil)
              (moss--semantic-query "symbols" nil)
              (should (= invocations 1))
              (goto-char (point-max))
              (insert "\n")
              (moss--semantic-query "symbols" nil)
              (should (= invocations 2))
              (dolist (arguments argument-lists)
                (should (member "--overlay-source" arguments)))))
        (set-buffer-modified-p nil)
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-unsaved-xref-and-references ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_shadow.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-min))
            (search-forward "value")
            (should (= 2 (xref-file-location-line
                          (xref-item-location
                           (car (xref-backend-definitions 'moss "value"))))))
            (goto-char (point-min))
            (insert "\n")
            (forward-line 2)
            (search-forward "value" (line-end-position))
            (let* ((definition
                    (car (xref-backend-definitions 'moss "value")))
                   (references (xref-backend-references 'moss "value")))
              (should (= 3 (xref-file-location-line
                            (xref-item-location definition))))
              (should (equal '(4 5)
                             (mapcar
                              (lambda (xref)
                                (xref-file-location-line
                                 (xref-item-location xref)))
                              references)))))
        (set-buffer-modified-p nil)
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-unsaved-symbols-and-calls ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_ide/src/main.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-min))
            (search-forward "middle(value)")
            (replace-match "leaf(value)" t t)
            (goto-char (point-min))
            (search-forward "top")
            (let* ((calls (moss--calls-at-point))
                   (targets
                    (mapcar (lambda (edge) (moss--json-get 'target edge))
                            (moss--json-get 'direct_calls calls))))
              (should-not (member "fn:middle" targets))
              (should (= 2 (cl-count "fn:leaf" targets :test #'equal))))
            (goto-char (point-min))
            (while (search-forward "top" nil t)
              (replace-match "summit" t t))
            (let ((names
                   (mapcar
                    (lambda (symbol)
                      (moss--json-get 'qualified_name symbol))
                    (moss--json-get
                     'symbols (moss--semantic-query "symbols" "")))))
              (should (member "summit" names))
              (should-not (member "top" names))))
        (set-buffer-modified-p nil)
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-compiler-invalid-unsaved-source-fails-explicitly ()
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name
                  "tests/tooling/fixtures/phase225_shadow.moss" root))
         (moss-compiler-command (expand-file-name "moss" root)))
    (with-current-buffer (find-file-noselect source)
      (unwind-protect
          (progn
            (moss-mode)
            (goto-char (point-min))
            (insert "fn broken(:\n")
            (should-error
             (moss--semantic-query "references" "fn:left")
             :type 'moss-semantic-error))
        (set-buffer-modified-p nil)
        (kill-buffer (current-buffer))))))

(ert-deftest moss-mode-real-examples-exhaustive-browsing-matrix ()
  (let* ((root (moss--repo-root default-directory))
         (projects (expand-file-name "examples/projects" root))
         (sources (directory-files-recursively projects "\\.moss\\'"))
         (manifests
          (directory-files-recursively
           projects "\\(?:Moss\\|moss\\)\\.toml\\'"))
         (moss-compiler-command (expand-file-name "moss" root))
         (callable-kinds '("function" "method" "handler" "main"))
         reference-sites
         (checked-symbols 0)
         (checked-callables 0))
    (should sources)
    (should manifests)
    (dolist (manifest manifests)
      (should
       (cl-some
        (lambda (source)
          (string-prefix-p (file-name-directory manifest) source))
        sources)))
    (dolist (source sources)
      (should
       (or (locate-dominating-file source "moss.toml")
           (locate-dominating-file source "Moss.toml")))
      (with-current-buffer (find-file-noselect source)
        (unwind-protect
            (progn
              (moss-mode)
              (let* ((symbols
                      (moss--json-get
                       'symbols (moss--semantic-query "symbols" nil)))
                     (local-symbols
                      (cl-remove-if-not
                       (lambda (item)
                         (equal (file-truename source)
                                (file-truename
                                 (moss--json-get
                                  'file (moss--json-get 'source item)))))
                       symbols)))
                (should local-symbols)
                (dolist (symbol local-symbols)
                  (setq checked-symbols (1+ checked-symbols))
                  (let* ((id (moss--json-get 'entity_id symbol))
                         (kind (moss--json-get 'kind symbol))
                         (label (moss--json-get 'display_name symbol))
                         (location (moss--json-get 'source symbol))
                         (line (moss--json-get 'line location))
                         (references
                          (moss--semantic-query "references" id))
                         (definition
                          (moss--json-get 'source
                                          (moss--json-get
                                           'definition references)))
                         (line-has-label nil))
                    (goto-char (point-min))
                    (forward-line (1- line))
                    (setq line-has-label
                          (search-forward label (line-end-position) t))
                    (when line-has-label
                      (backward-char (length label))
                      (should (equal id
                                     (moss--json-get
                                      'id (moss--entity-at-point))))
                      (let* ((xref
                              (car (xref-backend-definitions 'moss label)))
                             (xref-location (xref-item-location xref)))
                        (should
                         (equal (file-truename
                                 (moss--json-get 'file definition))
                                (file-truename
                                 (xref-file-location-file xref-location))))
                        (should (= (moss--json-get 'line definition)
                                   (xref-file-location-line xref-location))))
                      (let ((expected
                             (cl-count-if
                              (lambda (item)
                                (not (equal
                                      "declaration"
                                      (moss--json-get 'kind item))))
                              (moss--json-get 'references references))))
                        (should (= expected
                                   (length
                                    (xref-backend-references 'moss label)))))
                      (when (member kind callable-kinds)
                        (setq checked-callables (1+ checked-callables))
                        (let ((calls (moss--calls-at-point)))
                          (should
                           (= (length (moss--json-get 'callers calls))
                              (length
                               (moss-test--capture-xrefs #'moss-callers))))
                          (should
                           (= (length (moss--json-get 'direct_calls calls))
                              (length
                               (moss-test--capture-xrefs #'moss-callees)))))))
                    (dolist (reference (moss--json-get
                                        'references references))
                      (unless (equal "declaration"
                                     (moss--json-get 'kind reference))
                        (push
                         (list
                          (moss--json-get
                           'file (moss--json-get 'source reference))
                          (moss--json-get
                           'line (moss--json-get 'source reference))
                          label
                          (moss--json-get 'file definition)
                          (moss--json-get 'line definition))
                         reference-sites)))))))
          (kill-buffer (current-buffer)))))
    (should (> checked-symbols 0))
    (should (> checked-callables 0))
    ;; Every compiler-reported use is also a working at-point definition jump,
    ;; including imports, qualified module tokens, calls, constructors, and
    ;; type uses across physical project files.
    (dolist (site reference-sites)
      (ert-info ((format "reference site %S" site))
        (with-current-buffer (find-file-noselect (nth 0 site))
          (unwind-protect
              (progn
                (moss-mode)
                (moss-test--goto-line-token (nth 1 site) (nth 2 site))
                (let* ((xref
                        (car (xref-backend-definitions 'moss (nth 2 site))))
                       (location (xref-item-location xref)))
                  (should
                   (equal (file-truename (nth 3 site))
                          (file-truename
                           (xref-file-location-file location))))
                  (should (= (nth 4 site)
                             (xref-file-location-line location)))))
            (kill-buffer (current-buffer))))))))

(ert-deftest moss-mode-dependent-package-cross-module-navigation ()
  "Verify Emacs xref, calls, callers, callees, and symbol search cross dependent package boundaries."
  (let* ((root (moss--repo-root default-directory))
         (planner-main (expand-file-name "projects/build_planner/planner/src/main.moss" root))
         (planner-buffer (find-file-noselect planner-main))
         (moss-compiler-command (expand-file-name "moss" root)))
    (unwind-protect
        (with-current-buffer planner-buffer
          (moss-mode)
          ;; Verify project-wide symbols include both local planner and dependent graphlib symbols
          (let* ((symbols (moss--json-get 'symbols (moss--semantic-query "symbols" nil)))
                 (graphlib-symbols
                  (cl-remove-if-not
                   (lambda (sym)
                     (string-match-p "graphlib" (or (moss--json-get 'file (moss--json-get 'source sym)) "")))
                   symbols))
                 (planner-symbols
                  (cl-remove-if-not
                   (lambda (sym)
                     (string-match-p "planner" (or (moss--json-get 'file (moss--json-get 'source sym)) "")))
                   symbols)))
            (should (> (length graphlib-symbols) 0))
            (should (> (length planner-symbols) 0))
            ;; Test xref-backend-apropos finds symbols across both packages
            (let ((apropos-graph (xref-backend-apropos 'moss "make_graph")))
              (should apropos-graph)
              (let ((loc (xref-item-location (car apropos-graph))))
                (should (string-match-p "graph\\.moss" (xref-file-location-file loc)))))
            ;; Move point to 'model.summary_total_cost' in main.moss and jump to definition in graphlib/src/model.moss
            (goto-char (point-min))
            (search-forward "summary_total_cost")
            (backward-char (length "summary_total_cost"))
            (let* ((xrefs (xref-backend-definitions 'moss "summary_total_cost"))
                   (def-loc (xref-item-location (car xrefs))))
              (should def-loc)
              (should (string-match-p "graphlib/src/model\\.moss" (xref-file-location-file def-loc)))
              (should (= 64 (xref-file-location-line def-loc))))
            ;; Test callers and callees on main function
            (goto-char (point-min))
            (search-forward "fn main")
            (backward-char (length "main"))
            (let ((callees (moss-test--capture-xrefs #'moss-callees)))
              (should (> (length callees) 0))
              (should (cl-some (lambda (xref)
                                 (string-match-p "planner" (xref-file-location-file (xref-item-location xref))))
                               callees)))))
      (when (get-buffer "*Moss Call Tree*")
        (kill-buffer "*Moss Call Tree*"))
      (when (buffer-live-p planner-buffer)
        (kill-buffer planner-buffer)))))

(ert-deftest moss-mode-dependent-package-check-and-compilation ()
  "Verify Emacs check and compile commands propagate MOSS_MODULE_PATH for dependent packages."
  (let* ((root (moss--repo-root default-directory))
         (planner-main (expand-file-name "projects/build_planner/planner/src/main.moss" root))
         (planner-buffer (find-file-noselect planner-main))
         (moss-compiler-command (expand-file-name "moss" root)))
    (unwind-protect
        (with-current-buffer planner-buffer
          (moss-mode)
          (let ((mod-paths (moss--dependency-module-paths planner-main))
                (src-roots (moss--dependency-source-roots planner-main)))
            (should mod-paths)
            (should (string-match-p "graphlib" mod-paths))
            (should src-roots)
            (should (string-match-p "graphlib" src-roots)))
          (let ((cmd (moss--check-command planner-main)))
            (should (string-match-p "--diagnostic-paths --check" cmd))))
      (when (buffer-live-p planner-buffer)
        (kill-buffer planner-buffer)))))

(ert-deftest moss-mode-goto-generated-rust-navigation ()
  "Verify moss-goto-generated-rust finds the mapped line in generated Rust."
  (let* ((root (moss--repo-root default-directory))
         (source (expand-file-name "examples/counter.moss" root))
         (buffer (find-file-noselect source))
         (moss-compiler-command (expand-file-name "moss" root)))
    (unwind-protect
        (with-current-buffer buffer
          (moss-mode)
          ;; Build debug artifacts
          (let* ((artifacts (moss--prepare-artifacts source))
                 (cmd (moss--debug-build-command source)))
            (call-process-shell-command cmd)
            (should (file-exists-p (alist-get 'map artifacts)))
            (should (file-exists-p (alist-get 'rust artifacts)))
            ;; Move to line with "fn Add"
            (goto-char (point-min))
            (search-forward "fn Add")
            (let* ((line (line-number-at-pos))
                   (map-file (moss--source-map-file source))
                   (doc (moss--read-map map-file))
                   (entry (car (moss--entries-at-source doc source line))))
              (should entry)
              (let* ((gen (moss--json-get 'generated entry))
                     (gen-file (moss--json-get 'file gen))
                     (gen-line (moss--entry-generated-line entry line)))
                (should (file-exists-p gen-file))
                (should (> gen-line 0))))))
      (when (buffer-live-p buffer)
        (kill-buffer buffer)))))

(ert-deftest moss-mode-retired-domain-spellings-are-warnings ()
  (with-temp-buffer
    (insert "spawn Worker()\nawait worker.Run()\nmessage worker.Run()\ndomainroutes(worker: Worker)\n")
    (moss-mode)
    (font-lock-ensure)
    (goto-char (point-min))
    (should (eq (get-text-property (point) 'face) 'font-lock-warning-face))
    (forward-line 1)
    (should (eq (get-text-property (point) 'face) 'font-lock-warning-face))
    (forward-line 1)
    (should (eq (get-text-property (point) 'face) 'font-lock-keyword-face))
    (forward-line 1)
    (should (eq (get-text-property (point) 'face) 'font-lock-keyword-face))))

(provide 'moss-mode-tests)

;;; moss-mode-tests.el ends here
