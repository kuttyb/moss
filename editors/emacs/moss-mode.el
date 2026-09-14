;;; moss-mode.el --- Major mode and tooling for Moss -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Moss contributors
;; Author: Moss contributors
;; Keywords: languages, tools
;; Package-Requires: ((emacs "29.1"))

;;; Commentary:

;; Dependency-light editing, compilation, source-map navigation, LLDB/DAP, and
;; native disassembly support for Moss.  All navigation consumes the compiler's
;; .mossmap artifact; this mode never reverse-engineers generated Rust.

;;; Code:

(require 'cl-lib)
(require 'compile)
(require 'imenu)
(require 'json)
(require 'subr-x)

(declare-function dape "dape" (config &optional skip-compile))

(defconst moss--installation-root
  (expand-file-name
   "../.." (file-name-directory (or load-file-name buffer-file-name
                                    default-directory)))
  "Moss checkout or installation root containing this major mode.")

(defgroup moss nil
  "Editing and inspecting Moss programs."
  :group 'languages
  :prefix "moss-")

(defcustom moss-compiler-command nil
  "Moss compiler executable, or nil to find the repository compiler."
  :type '(choice (const :tag "Find automatically" nil) file))

(defcustom moss-rustc-command "rustc"
  "Rust compiler used to build generated Moss programs."
  :type 'string)

(defcustom moss-build-directory "build/emacs"
  "Repository-relative directory for artifacts created by `moss-mode'."
  :type 'string)

(defcustom moss-compile-optimization "-O"
  "Optimization option passed by `moss-compile-buffer'."
  :type '(choice (const "-O") (const "-O0") string))

(defcustom moss-lldb-dap-command nil
  "lldb-dap executable, or nil to find it on PATH."
  :type '(choice (const :tag "Find automatically" nil) file))

(defcustom moss-lldb-script nil
  "Moss LLDB bridge script, or nil to find it in the Moss installation."
  :type '(choice (const :tag "Find automatically" nil) file))

(defcustom moss-follow-dape-stops t
  "When non-nil, show exact mapped Moss source locations at DAP stops.
The generated Rust buffer remains available to dape, but Moss source is
displayed whenever the shared map contains an exact line mapping.  Ambiguous
or range-only optimized locations are not presented as precise Moss steps."
  :type 'boolean)

(defcustom moss-objdump-command nil
  "Objdump executable, or nil to prefer llvm-objdump and then objdump."
  :type '(choice (const :tag "Find automatically" nil) file))

(defconst moss--declaration-keywords
  '("fn" "proc" "type" "trait" "domain" "on" "let" "var"))

(defconst moss--control-keywords
  '("if" "else" "while" "return" "and" "or" "not"))

(defconst moss--concurrency-keywords
  '("message" "await" "reply" "spawn"))

(defconst moss--builtin-types
  '("Int" "Float" "Bool" "String" "Vector" "Map" "Queue"
    "Option" "Result" "int" "float" "bool" "string" "vector"
    "map" "queue" "seq" "option" "table"))

(defconst moss--functional-operations
  '("map" "filter" "reduce" "sum" "count" "any" "all"))

(defvar moss-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\\ "\\" table)
    (modify-syntax-entry ?_ "w" table)
    table)
  "Syntax table for `moss-mode'.")

(defconst moss-font-lock-keywords
  `((,(regexp-opt (append moss--declaration-keywords
                          moss--control-keywords
                          moss--concurrency-keywords)
                  'symbols)
     . font-lock-keyword-face)
    (,(regexp-opt moss--functional-operations 'symbols)
     . font-lock-builtin-face)
    (,(regexp-opt moss--builtin-types 'symbols) . font-lock-type-face)
    ("^[[:space:]]*\\(?:fn\\|proc\\)[[:space:]]+\\([[:word:]_]+\\)"
     1 font-lock-function-name-face)
    ("^[[:space:]]*on[[:space:]]+\\([[:word:]_]+\\)"
     1 font-lock-function-name-face)
    ("^[[:space:]]*\\(?:type\\|trait\\|domain\\)[[:space:]]+\\([[:word:]_]+\\)"
     1 font-lock-type-face)
    ("\\_<\\(?:true\\|false\\)\\_>" . font-lock-constant-face)
    ("\\_<[0-9]+\\(?:\\.[0-9]+\\)?\\_>" . font-lock-constant-face))
  "Font-lock rules for Moss source.")

(defconst moss--definition-regexp
  "^[[:space:]]*\\(?:fn\\|proc\\|on\\|type\\|trait\\|domain\\)\\_>")

(defun moss--line-indentation ()
  "Return the indentation of the current line without moving point."
  (save-excursion
    (back-to-indentation)
    (current-column)))

(defun moss--previous-code-line ()
  "Move to the previous nonblank, non-comment-only line.
Return non-nil when such a line exists."
  (let ((found nil))
    (while (and (not found) (= (forward-line -1) 0))
      (back-to-indentation)
      (unless (or (eolp) (looking-at-p "#"))
        (setq found t)))
    found))

(defun moss--calculate-indentation ()
  "Calculate conservative two-space indentation for the current line."
  (save-excursion
    (back-to-indentation)
    (let ((dedent (looking-at-p "else\\_>"))
          (top-level-declaration
           (and (= (current-indentation) 0)
                (looking-at-p
                 "\\(?:fn\\|proc\\|type\\|trait\\|domain\\)\\_>")))
          (existing (current-indentation)))
      (if (not (moss--previous-code-line))
          0
        (let* ((previous-indent (moss--line-indentation))
               (opens-block
                (save-excursion
                  (end-of-line)
                  (skip-chars-backward " \t")
                  (eq (char-before) ?:)))
               (indent (+ previous-indent (if opens-block 2 0))))
          (setq indent (max 0 (- indent (if dedent 2 0))))
          (cond (top-level-declaration 0)
                ((and (> existing 0) (< existing indent)) existing)
                (t indent)))))))

(defun moss-indent-line ()
  "Indent the current Moss line using two-space block indentation."
  (interactive)
  (let ((offset (- (current-column) (current-indentation)))
        (indent (moss--calculate-indentation)))
    (indent-line-to indent)
    (when (> offset 0) (move-to-column (+ indent offset)))))

(defun moss--imenu-add (table category name position)
  "Add NAME at POSITION under CATEGORY in TABLE."
  (let ((items (gethash category table)))
    (puthash category (cons (cons name position) items) table)))

(defun moss-imenu-create-index ()
  "Build an Imenu index for Moss functions, types, domains, and handlers."
  (let ((table (make-hash-table :test #'equal))
        (containers nil))
    (save-excursion
      (goto-char (point-min))
      (while (re-search-forward
              "^\\([ ]*\\)\\(type\\|trait\\|domain\\|fn\\|proc\\|on\\)[ ]+\\([[:word:]_]+\\)"
              nil t)
        (let* ((indent (length (match-string-no-properties 1)))
               (kind (match-string-no-properties 2))
               (name (match-string-no-properties 3))
               (position (copy-marker (match-beginning 0))))
          (while (and containers (>= (caar containers) indent))
            (pop containers))
          (pcase kind
            ((or "type" "trait")
             (moss--imenu-add table "Types and Traits" name position)
             (push (list indent kind name) containers))
            ("domain"
             (moss--imenu-add table "Domains" name position)
             (push (list indent kind name) containers))
            ("on"
             (moss--imenu-add table "Handlers" name position))
            ((or "fn" "proc")
             (let ((parent (cadr (car containers))))
               (moss--imenu-add
                table
                (cond ((equal parent "domain") "Handlers")
                      ((member parent '("type" "trait")) "Methods")
                      (t "Functions"))
                name position)))))))
    (let (index)
      (dolist (category '("Functions" "Types and Traits" "Domains"
                          "Methods" "Handlers"))
        (when-let ((items (gethash category table)))
          (push (cons category (nreverse items)) index)))
      (nreverse index))))

(defun moss-beginning-of-defun (&optional argument)
  "Move to the beginning of a Moss definition.
With negative ARGUMENT, move forward instead."
  (interactive "p")
  (let ((argument (or argument 1)))
    (if (< argument 0)
        (moss-end-of-defun (- argument))
      (dotimes (_ argument)
        (unless (re-search-backward moss--definition-regexp nil 'move)
          (goto-char (point-min)))))))

(defun moss-end-of-defun (&optional argument)
  "Move to the end of a Moss definition."
  (interactive "p")
  (dotimes (_ (or argument 1))
    (beginning-of-line)
    (unless (looking-at moss--definition-regexp)
      (re-search-backward moss--definition-regexp nil 'move))
    (let ((indent (moss--line-indentation)))
      (forward-line 1)
      (while (and (not (eobp))
                  (or (looking-at-p "^[[:space:]]*$")
                      (> (moss--line-indentation) indent)))
        (forward-line 1)))))

(defun moss--repo-root (&optional start)
  "Return the Moss repository root containing START."
  (or (locate-dominating-file (or start default-directory) "src/moss.cpp")
      (locate-dominating-file (or start default-directory) ".git")
      default-directory))

(defun moss--compiler (&optional root)
  "Return the configured or repository-local Moss compiler."
  (or moss-compiler-command
      (let ((candidate (expand-file-name "moss" (or root (moss--repo-root)))))
        (and (file-executable-p candidate) candidate))
      (let ((candidate (expand-file-name "moss" moss--installation-root)))
        (and (file-executable-p candidate) candidate))
      (executable-find "moss")
      (user-error "Build Moss first or customize `moss-compiler-command'")))

(defun moss--source-file ()
  "Return and save the current Moss source file."
  (unless (and buffer-file-name
               (string-equal (file-name-extension buffer-file-name) "moss"))
    (user-error "Current buffer is not visiting a .moss file"))
  (save-buffer)
  (expand-file-name buffer-file-name))

(defun moss--artifact-alist (source)
  "Return the generated artifact paths for SOURCE."
  (let* ((root (moss--repo-root source))
         (stem (file-name-base source))
         (directory (expand-file-name
                     (concat (file-name-as-directory moss-build-directory) stem)
                     root))
         (base (expand-file-name stem directory)))
    `((root . ,root)
      (directory . ,directory)
      (rust . ,(concat base ".rs"))
      (map . ,(concat base ".mossmap"))
      (executable . ,base))))

(defun moss--prepare-artifacts (source)
  "Create and return the artifact paths for SOURCE."
  (let ((artifacts (moss--artifact-alist source)))
    (make-directory (alist-get 'directory artifacts) t)
    artifacts))

(defun moss--shell-command (&rest arguments)
  "Quote and join ARGUMENTS as a shell command."
  (mapconcat #'shell-quote-argument arguments " "))

(defun moss--check-command (source)
  "Return the compilation command used to check SOURCE."
  (moss--shell-command (moss--compiler (moss--repo-root source))
                       "--diagnostic-paths" "--check" source))

(defun moss--native-command (source &optional run)
  "Return the normal compile command for SOURCE.
When RUN is non-nil, execute the resulting program too."
  (let* ((artifacts (moss--prepare-artifacts source))
         (compiler-command
          (moss--shell-command
           (moss--compiler (alist-get 'root artifacts))
           "--diagnostic-paths" moss-compile-optimization source
           "--native-output" (alist-get 'executable artifacts)
           "-o" (alist-get 'rust artifacts)))
         (rust-command
          (moss--shell-command
           moss-rustc-command "-D" "warnings" "-g" "-C" "debuginfo=2"
           (alist-get 'rust artifacts)
           "-o" (alist-get 'executable artifacts)))
         (command (concat compiler-command " && " rust-command)))
    (if run
        (concat command " && "
                (moss--shell-command (alist-get 'executable artifacts)))
      command)))

(defun moss--debug-build-command (source)
  "Return a source-fidelity native debug-build command for SOURCE."
  (let* ((artifacts (moss--prepare-artifacts source))
         (compiler-command
          (moss--shell-command
           (moss--compiler (alist-get 'root artifacts))
           "--debug" "--diagnostic-paths"
           "--emit-debug-map" (alist-get 'map artifacts)
           "--native-output" (alist-get 'executable artifacts)
           source "-o" (alist-get 'rust artifacts)))
         (rust-command
          (moss--shell-command
           moss-rustc-command "-D" "warnings" "-g" "-C" "debuginfo=2"
           "-C" "opt-level=0" "-C" "strip=none"
           "-C" "force-frame-pointers=yes"
           (alist-get 'rust artifacts) "-o"
           (alist-get 'executable artifacts))))
    (concat compiler-command " && " rust-command)))

(defun moss--compilation-start (command name)
  "Start COMMAND in compilation mode with a buffer named from NAME."
  (let ((default-directory (moss--repo-root)))
    (compilation-start command 'compilation-mode
                       (lambda (_) (format "*moss-%s*" name)))))

;;;###autoload
(defun moss-check-buffer ()
  "Check the current Moss buffer and show clickable diagnostics."
  (interactive)
  (moss--compilation-start (moss--check-command (moss--source-file)) "check"))

;;;###autoload
(defun moss-compile-buffer ()
  "Compile the current Moss buffer to a native executable."
  (interactive)
  (moss--compilation-start (moss--native-command (moss--source-file)) "compile"))

;;;###autoload
(defun moss-run-buffer ()
  "Compile and run the current Moss buffer in compilation mode."
  (interactive)
  (moss--compilation-start (moss--native-command (moss--source-file) t) "run"))

;;;###autoload
(defun moss-build-debug-buffer ()
  "Build an unoptimized Moss executable with debug info and stable symbols."
  (interactive)
  (moss--compilation-start
   (moss--debug-build-command (moss--source-file)) "debug-build"))

(defun moss--display-artifact (filename mode)
  "Visit FILENAME in another window, make it read-only, and use MODE."
  (unless (file-readable-p filename)
    (user-error "Artifact does not exist; compile the Moss buffer first: %s"
                filename))
  (let ((buffer (find-file-other-window filename)))
    (with-current-buffer buffer
      (funcall mode)
      (read-only-mode 1))
    buffer))

;;;###autoload
(defun moss-show-generated-rust ()
  "Show generated Rust for the current Moss source."
  (interactive)
  (let* ((source (moss--source-file))
         (rust (alist-get 'rust (moss--artifact-alist source))))
    (moss--display-artifact rust
                            (if (fboundp 'rust-ts-mode)
                                #'rust-ts-mode #'prog-mode))))

;;;###autoload
(defun moss-show-debug-map ()
  "Show the compiler-emitted debug/provenance map read-only."
  (interactive)
  (let* ((source (moss--source-file))
         (map (alist-get 'map (moss--artifact-alist source))))
    (moss--display-artifact map
                            (if (fboundp 'js-json-mode)
                                #'js-json-mode #'js-mode))))

(defun moss--json-get (key object)
  "Get KEY from JSON alist OBJECT across supported Emacs JSON key forms."
  (or (alist-get key object)
      (alist-get (symbol-name key) object nil nil #'equal)))

(defun moss--read-map (filename)
  "Read and validate one Moss map from FILENAME."
  (unless (file-readable-p filename)
    (user-error "Moss debug map not found: %s" filename))
  (with-temp-buffer
    (insert-file-contents filename)
    (let ((document (json-parse-buffer :object-type 'alist :array-type 'list
                                       :null-object nil :false-object nil)))
      (unless (and (equal (moss--json-get 'format document) "moss-debug-map")
                   (= (moss--json-get 'version document) 1))
        (user-error "Unsupported Moss debug map: %s" filename))
      document)))

(defun moss--same-file-p (left right)
  "Return non-nil when LEFT and RIGHT name the same local path."
  (and left right
       (string-equal (file-truename left) (file-truename right))))

(defun moss--source-entry-p (entry source line)
  "Return non-nil when ENTRY contains SOURCE at LINE."
  (let ((span (moss--json-get 'source entry)))
    (and (moss--same-file-p source (moss--json-get 'file span))
         (<= (moss--json-get 'start_line span) line)
         (<= line (moss--json-get 'end_line span)))))

(defun moss--entry-generated-line (entry moss-line)
  "Return ENTRY's generated line corresponding to MOSS-LINE."
  (or (cl-loop for mapping in (moss--json-get 'line_mappings entry)
               when (= (moss--json-get 'moss_line mapping) moss-line)
               return (moss--json-get 'generated_rust_line mapping))
      (moss--json-get 'start_line (moss--json-get 'generated entry))))

(defun moss--exact-source-mapping-p (document source line)
  "Return non-nil when DOCUMENT maps SOURCE LINE exactly.
Range-only provenance remains useful for navigation, but is insufficient for
a source breakpoint because it cannot promise an exact Moss stop."
  (cl-some
   (lambda (entry)
     (and (moss--source-entry-p entry source line)
          (cl-some
           (lambda (mapping)
             (= (moss--json-get 'moss_line mapping) line))
           (moss--json-get 'line_mappings entry))))
   (moss--json-get 'entries document)))

(defun moss--entries-at-source (document source line)
  "Return deterministic DOCUMENT entries covering SOURCE at LINE."
  (sort (cl-remove-if-not
         (lambda (entry) (moss--source-entry-p entry source line))
         (copy-sequence (moss--json-get 'entries document)))
        (lambda (left right)
          (let* ((left-source (moss--json-get 'source left))
                 (right-source (moss--json-get 'source right))
                 (left-width (- (moss--json-get 'end_line left-source)
                                (moss--json-get 'start_line left-source)))
                 (right-width (- (moss--json-get 'end_line right-source)
                                 (moss--json-get 'start_line right-source))))
            (if (/= left-width right-width)
                (< left-width right-width)
              (string-lessp (moss--json-get 'semantic_identity left)
                            (moss--json-get 'semantic_identity right)))))))

(defun moss--source-map-file (source)
  "Return the expected Moss map for SOURCE."
  (alist-get 'map (moss--artifact-alist source)))

;;;###autoload
(defun moss-goto-generated-rust ()
  "Jump from the current Moss line to its mapped generated Rust location."
  (interactive)
  (let* ((source (moss--source-file))
         (line (line-number-at-pos))
         (map-file (moss--source-map-file source))
         (document (moss--read-map map-file))
         (entry (car (moss--entries-at-source document source line))))
    (unless entry (user-error "No generated mapping for this Moss line"))
    (let* ((generated (moss--json-get 'generated entry))
           (filename (moss--json-get 'file generated))
           (generated-line (moss--entry-generated-line entry line)))
      (find-file-other-window filename)
      (goto-char (point-min))
      (forward-line (1- generated-line))
      (back-to-indentation))))

(defun moss--adjacent-map-file (generated-file)
  "Return the map file adjacent to GENERATED-FILE."
  (concat (file-name-sans-extension generated-file) ".mossmap"))

(defun moss--entry-source-line-for-generated (entry generated-line)
  "Return ENTRY's exact Moss line for GENERATED-LINE, or nil."
  (cl-loop for mapping in (moss--json-get 'line_mappings entry)
           when (= (moss--json-get 'generated_rust_line mapping)
                   generated-line)
           return (moss--json-get 'moss_line mapping)))

(defun moss--generated-origin (document generated-file line &optional exact-only)
  "Return the best Moss origin in DOCUMENT for GENERATED-FILE at LINE.
The result is (ENTRY MOSS-LINE EXACT).  When EXACT-ONLY is non-nil, ignore
range-only provenance; this prevents debugger stepping from claiming source
precision which the generated program does not provide."
  (let ((candidates
         (cl-loop for entry in (moss--json-get 'entries document)
                  for range = (moss--json-get 'generated entry)
                  for exact = (moss--entry-source-line-for-generated entry line)
                  when (and (moss--same-file-p generated-file
                                               (moss--json-get 'file range))
                            (<= (moss--json-get 'start_line range) line)
                            (<= line (moss--json-get 'end_line range))
                            (or exact (not exact-only)))
                  collect
                  (let* ((source (moss--json-get 'source entry))
                         (width (- (moss--json-get 'end_line source)
                                   (moss--json-get 'start_line source))))
                    (list (if exact 0 1) width
                          (moss--json-get 'semantic_identity entry)
                          entry
                          (or exact (moss--json-get 'start_line source))
                          (and exact t))))))
    (setq candidates
          (sort candidates
                (lambda (left right)
                  (cond ((/= (nth 0 left) (nth 0 right))
                         (< (nth 0 left) (nth 0 right)))
                        ((/= (nth 1 left) (nth 1 right))
                         (< (nth 1 left) (nth 1 right)))
                        (t (string-lessp (nth 2 left) (nth 2 right)))))))
    (when candidates
      (let ((selected (car candidates)))
        (list (nth 3 selected) (nth 4 selected) (nth 5 selected))))))

;;;###autoload
(defun moss-jump-to-moss-source ()
  "Jump from generated Rust to an originating Moss construct."
  (interactive)
  (unless buffer-file-name (user-error "Current buffer has no file"))
  (let* ((generated-file (expand-file-name buffer-file-name))
         (line (line-number-at-pos))
         (document (moss--read-map
                    (moss--adjacent-map-file generated-file)))
         (origin (moss--generated-origin document generated-file line)))
    (unless origin (user-error "No Moss provenance at this Rust line"))
    (let* ((entry (nth 0 origin))
           (moss-line (nth 1 origin))
           (source (moss--json-get 'source entry)))
      (find-file-other-window (moss--json-get 'file source))
      (goto-char (point-min))
      (forward-line (1- moss-line))
      (back-to-indentation))))

(defface moss-debug-location-face
  '((t :inherit highlight :extend t))
  "Face used for the exact Moss location corresponding to a DAP stop."
  :group 'moss)

(defvar moss--active-debug-map nil
  "Debug map used to mirror the active dape session into Moss source.")

(defvar moss--debug-location-overlay nil
  "Overlay showing the current exact Moss debug location.")

(defun moss--dape-display-moss-source ()
  "Display the exact Moss origin of the current dape source location.
This function is suitable for `dape-display-source-hook', which runs in the
generated source buffer at the selected stack-frame line."
  (when (and moss-follow-dape-stops
             moss--active-debug-map
             buffer-file-name
             (file-readable-p moss--active-debug-map))
    (condition-case nil
        (let* ((document (moss--read-map moss--active-debug-map))
               (origin (moss--generated-origin
                        document (expand-file-name buffer-file-name)
                        (line-number-at-pos) t)))
          (when origin
            (let* ((entry (nth 0 origin))
                   (moss-line (nth 1 origin))
                   (source (moss--json-get 'source entry))
                   (buffer (find-file-noselect (moss--json-get 'file source))))
              (when (overlayp moss--debug-location-overlay)
                (delete-overlay moss--debug-location-overlay))
              (with-current-buffer buffer
                (goto-char (point-min))
                (forward-line (1- moss-line))
                (setq moss--debug-location-overlay
                      (make-overlay (line-beginning-position)
                                    (line-beginning-position 2) buffer))
                (overlay-put moss--debug-location-overlay
                             'face 'moss-debug-location-face)
                (overlay-put moss--debug-location-overlay
                             'help-echo "Current exact Moss debug location")
                (when-let ((window
                            (display-buffer
                             buffer
                             '((display-buffer-reuse-window
                                display-buffer-same-window
                                display-buffer-use-some-window)))))
                  (set-window-point window (line-beginning-position)))))))
      (error nil))))

(defface moss-breakpoint-face
  '((t :inherit error :extend t))
  "Face used for pending Moss source breakpoints."
  :group 'moss)

(defvar-local moss--breakpoint-overlays nil
  "Pending Moss breakpoints translated when `moss-debug' starts.")

(defun moss--breakpoint-at-line (line)
  "Return a pending Moss breakpoint overlay at LINE."
  (cl-find-if
   (lambda (overlay)
     (= line (line-number-at-pos (overlay-start overlay))))
   moss--breakpoint-overlays))

;;;###autoload
(defun moss-toggle-breakpoint ()
  "Toggle a source breakpoint on the current Moss line.
Breakpoints are translated through the .mossmap when `moss-debug' starts."
  (interactive)
  (unless (derived-mode-p 'moss-mode)
    (user-error "Moss breakpoints must be placed in a Moss source buffer"))
  (let* ((line (line-number-at-pos))
         (existing (moss--breakpoint-at-line line)))
    (if existing
        (progn
          (setq moss--breakpoint-overlays
                (delq existing moss--breakpoint-overlays))
          (delete-overlay existing)
          (message "Removed Moss breakpoint at line %d" line))
      (let ((overlay (make-overlay (line-beginning-position)
                                   (line-beginning-position 2))))
        (overlay-put overlay 'face 'moss-breakpoint-face)
        (overlay-put overlay 'help-echo "Pending Moss source breakpoint")
        (push overlay moss--breakpoint-overlays)
        (message "Added Moss breakpoint at line %d" line)))))

(defun moss--lldb-dap ()
  "Return an lldb-dap executable or report a useful error."
  (let ((program (or moss-lldb-dap-command
                     (executable-find "lldb-dap")
                     (moss--versioned-executable "lldb-dap"))))
    (unless program
      (user-error "lldb-dap not found; install LLDB's DAP adapter"))
    (unless (file-executable-p program)
      (user-error "lldb-dap is not executable: %s" program))
    program))

(defun moss--versioned-executable (name)
  "Return the newest executable /usr/bin/NAME-VERSION, if one exists."
  (when (file-directory-p "/usr/bin")
    (car
     (sort
      (cl-remove-if-not
       #'file-executable-p
      (directory-files
        "/usr/bin" t
        (concat "\\`" (regexp-quote name) "-[0-9]+\\'") t))
      (lambda (left right)
        (> (string-to-number
            (substring (file-name-nondirectory left) (1+ (length name))))
           (string-to-number
            (substring (file-name-nondirectory right) (1+ (length name))))))))))

(defun moss--lldb-script ()
  "Return the configured or installation-local Moss LLDB bridge."
  (let ((script
         (or moss-lldb-script
             (expand-file-name "tools/moss_lldb.py"
                               moss--installation-root))))
    (unless (file-readable-p script)
      (user-error
       "Moss LLDB Python helper not found: %s" script))
    script))

(defun moss--make-debug-config (source artifacts script adapter breakpoint-lines)
  "Build the Dape launch configuration validated by the raw DAP test.
ARTIFACTS supplies the native executable, map, and working directory.  SCRIPT
is the shared LLDB bridge, ADAPTER is lldb-dap, and BREAKPOINT-LINES contains
exact Moss source lines in deterministic order."
  (let ((executable (alist-get 'executable artifacts))
        (map-file (alist-get 'map artifacts))
        (root (alist-get 'root artifacts)))
    (list 'command adapter
          'command-cwd root
          :type "lldb-dap"
          :request "launch"
          :program executable
          :cwd root
          :initCommands
          (vector (format "command script import %S" script)
                  (format "moss-map-load %S" map-file))
          :preRunCommands
          (vconcat
           (mapcar
            (lambda (line)
              (format "moss-break %S" (format "%s:%d" source line)))
            breakpoint-lines))
          :stopOnEntry nil)))

;;;###autoload
(defun moss-debug ()
  "Launch the current debug build through dape and lldb-dap.
Use `moss-build-debug-buffer' first.  Pending Moss breakpoints are translated
to generated Rust locations by the shared compiler map before execution."
  (interactive)
  (unless (require 'dape nil t)
    (user-error "Install the optional Emacs package `dape' first"))
  (let* ((source (moss--source-file))
         (artifacts (moss--artifact-alist source))
         (executable (alist-get 'executable artifacts))
         (map-file (alist-get 'map artifacts)))
    (unless (file-executable-p executable)
      (user-error
       "Moss debug executable not found: %s; run M-x moss-build-debug-buffer"
       executable))
    (unless (file-readable-p map-file)
      (user-error
       "Moss debug map not found: %s; run M-x moss-build-debug-buffer"
       map-file))
    (let* ((document (moss--read-map map-file))
           (script (moss--lldb-script))
           (adapter (moss--lldb-dap))
           (breakpoint-lines
            (mapcar
             (lambda (overlay)
               (line-number-at-pos (overlay-start overlay)))
             (sort (copy-sequence moss--breakpoint-overlays)
                   (lambda (left right)
                     (< (overlay-start left) (overlay-start right)))))))
      (dolist (line breakpoint-lines)
        (unless (moss--exact-source-mapping-p document source line)
          (user-error
           "Moss breakpoint has no exact generated mapping: %s:%d"
           source line)))
      (let ((config (moss--make-debug-config
                     source artifacts script adapter breakpoint-lines)))
        (setq moss--active-debug-map map-file)
        (add-hook 'dape-display-source-hook #'moss--dape-display-moss-source)
        (condition-case error-data
            (dape config)
          (error
           (setq moss--active-debug-map nil)
           (remove-hook 'dape-display-source-hook
                        #'moss--dape-display-moss-source)
           (user-error "Moss DAP launch failed: %s"
                       (error-message-string error-data))))))))

(defun moss--objdump ()
  "Return the preferred native disassembler executable."
  (or moss-objdump-command
      (executable-find "llvm-objdump")
      (executable-find "objdump")
      (user-error "Install llvm-objdump or GNU objdump")))

(defun moss--native-entry-at-point (document source line)
  "Find the narrowest mapped native construct at SOURCE LINE."
  (cl-find-if
   (lambda (entry)
     (let ((symbol (moss--json-get 'native_symbol entry)))
       (and symbol (not (string-empty-p symbol)))))
   (moss--entries-at-source document source line)))

(defun moss--disassembly-arguments (program symbol executable)
  "Return objdump arguments for PROGRAM, SYMBOL, and EXECUTABLE."
  (if (string-match-p "llvm-objdump\\'" (file-name-nondirectory program))
      (list "--demangle" "--source" "--line-numbers"
            (concat "--disassemble-symbols=" symbol) executable)
    (list "--demangle" "--source" "--line-numbers"
          (concat "--disassemble=" symbol) executable)))

;;;###autoload
(defun moss-disassemble-at-point ()
  "Disassemble the native Moss function, method, or handler at point."
  (interactive)
  (let* ((source (moss--source-file))
         (line (line-number-at-pos))
         (artifacts (moss--artifact-alist source))
         (map-file (alist-get 'map artifacts))
         (document (moss--read-map map-file))
         (entry (moss--native-entry-at-point document source line))
         (executable (alist-get 'executable artifacts)))
    (unless entry
      (user-error "No stable native symbol here; make a debug build first"))
    (unless (file-executable-p executable)
      (user-error "Debug executable not found; run M-x moss-build-debug-buffer"))
    (let* ((program (moss--objdump))
           (symbol (moss--json-get 'native_symbol entry))
           (provenance (moss--json-get 'provenance entry))
           (buffer (get-buffer-create
                    (format "*moss-assembly:%s*" symbol))))
      (with-current-buffer buffer
        (let ((inhibit-read-only t))
          (erase-buffer)
          (insert (format "# Moss symbol: %s\n" symbol))
          (insert (format "# Semantic identity: %s\n"
                          (moss--json-get 'semantic_identity entry)))
          (insert (format "# Provenance: %s\n\n"
                          (mapconcat #'identity provenance ", ")))
          (let ((status (apply #'process-file program nil t nil
                               (moss--disassembly-arguments
                                program symbol executable))))
            (unless (zerop status)
              (insert (format "\n# objdump exited with status %s\n" status))))
          (asm-mode)
          (read-only-mode 1)
          (goto-char (point-min))))
      (pop-to-buffer buffer))))

;;;###autoload
(defalias 'moss-disassemble-function #'moss-disassemble-at-point)

;;;###autoload
(defalias 'moss-show-assembly #'moss-disassemble-at-point)

(defvar moss-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'moss-check-buffer)
    (define-key map (kbd "C-c C-b") #'moss-compile-buffer)
    (define-key map (kbd "C-c C-r") #'moss-run-buffer)
    (define-key map (kbd "C-c C-g") #'moss-goto-generated-rust)
    (define-key map (kbd "C-c C-d") #'moss-toggle-breakpoint)
    map)
  "Keymap for `moss-mode'.")

(add-to-list
 'compilation-error-regexp-alist-alist
 '(moss "^\\(.+\\.moss\\):\\([0-9]+\\): \\(?:warning\\|error\\):" 1 2))
(add-to-list 'compilation-error-regexp-alist 'moss)

;;;###autoload
(define-derived-mode moss-mode prog-mode "Moss"
  "Major mode for statically typed Moss source."
  :syntax-table moss-mode-syntax-table
  (setq-local font-lock-defaults '(moss-font-lock-keywords))
  (setq-local font-lock-keywords-case-fold-search nil)
  (setq-local indent-line-function #'moss-indent-line)
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width 2)
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local imenu-create-index-function #'moss-imenu-create-index)
  (setq-local beginning-of-defun-function #'moss-beginning-of-defun)
  (setq-local end-of-defun-function #'moss-end-of-defun))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.moss\\'" . moss-mode))

(provide 'moss-mode)

;;; moss-mode.el ends here
