/*
 * Setup page (first launch, or after the game files went missing). Setup cannot be skipped: Home is shown only
 * when app_state says "done". The page polls app_state while it is open and draws the state it reports.
 *
 * app_state().setup = {
 *   state: "needed" | "wrong_disc" | "checking" | "failed" | "files_missing" | "done",
 *   step: "copying" | "checking" | "preparing" | "renderer",   (while checking)
 *   progress: 0..1,                     (of the current step)
 *   bytes_done, bytes_total,            (copying, and the renderer download)
 *   renderer_step: "listing" | "downloading" | "installing" | "done" | "failed",
 *   disc_name, problem, note            (note: GSdx could not be downloaded; the game cannot start yet)
 * }
 * Flow: "Choose disc image" -> setup_choose_disc (Rust file picker and fast check) -> setup_start (copy, check,
 * prepare, renderer). A refused file shows its reason with "Choose another file". When setup is done the page
 * shows "Finish", which opens Home (it never starts the game). Commands: setup_choose_disc, setup_start, setup_cancel.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  var STEPS = [
    { id: "copying", label: "Copying game files", detail: "a copy of your disc image for GT4Recomp" },
    { id: "checking", label: "Checking disc", detail: null },
    { id: "preparing", label: "Preparing game files", detail: null },
    { id: "renderer", label: "Graphics renderer", detail: GT4.mac ? "GSdx (gt4-gs), included in the app" : "GSdx (gt4-gs) from GitHub" }
  ];
  var RENDERER_TEXT = {
    listing: "finding the latest version",
    downloading: "downloading",
    installing: "installing",
    done: "ready",
    failed: "not installed, the game cannot start yet"
  };
  var POLL_MS = 400;

  function call(command, args) {
    return GT4.backend.call(command, args);
  }

  function gigabytes(bytes) {
    return (bytes / 1e9).toFixed(1) + " GB";
  }

  function megabytes(bytes) {
    return (bytes / 1e6).toFixed(1) + " MB";
  }

  function barNode(pct) {
    var fill = h("i");
    fill.style.width = pct + "%";
    return h("span", { class: "bar" }, [fill]);
  }

  // One row: number, label (+ detail, bytes line, bar), value on the right.
  function row(number, label, detail, status, pct, bytesText) {
    var value = "";
    var valueClass = "v";
    if (status === "ok") {
      value = "✓";
      valueClass += " ok";
    } else if (status === "bad") {
      value = "Not supported";
      valueClass += " bad";
    } else if (status === "active" && pct !== null) {
      value = pct + "%";
    } else if (status === "active") {
      value = "…";
    }
    return h("li", { class: "step" + (status === "wait" ? " wait" : "") }, [
      h("span", { class: "n", text: number }),
      h("span", null, [
        h("span", { class: "t", text: label }),
        detail ? h("span", { class: "d", text: detail }) : null,
        bytesText ? h("span", { class: "bytes", text: bytesText }) : null,
        status === "active" && pct !== null ? barNode(pct) : null
      ]),
      h("span", { class: valueClass, text: value })
    ]);
  }

  // Signature decides whether the page has to be rebuilt.
  function signature(setup) {
    return [setup.state, setup.step, Math.round((setup.progress || 0) * 100), setup.renderer_step || "",
      Math.round((setup.bytes_done || 0) / 1e6), setup.problem || "", setup.note || "", setup.disc_name || ""].join("|");
  }

  function texts(state) {
    switch (state) {
      case "done":
        return ["Ready", "Press Finish to open GT4Recomp. Next time it opens straight to the home screen. You can move or delete the disc image you chose."];
      case "files_missing":
        return ["Game files missing", "Choose your disc image again to copy the game files. Your saves and settings are kept."];
      case "checking":
        return ["Setting up your game", "This only happens once. Keep GT4Recomp open until it finishes."];
      default:
        return ["Set up your game", "Choose your Gran Turismo 4 disc image (.iso). This only happens once."];
    }
  }

  function build(setup, parts) {
    var state = setup.state;
    var heading = texts(state);
    parts.title.textContent = heading[0];
    parts.sub.textContent = heading[1];

    var steps = [];
    var buttons = [];
    var hints = [];
    var order = STEPS.map(function (s) { return s.id; }).indexOf(setup.step);
    var running = state === "checking";
    var done = state === "done";
    var discName = setup.disc_name || "";

    if (state === "wrong_disc") {
      steps.push(row("01", "Disc", discName || null, "bad", null, null));
    } else if (running || done) {
      steps.push(row("01", "Disc", discName || null, "ok", null, null));
    } else {
      // Not chosen yet ("..." waits for the pick), or the file of a stopped run (no mark).
      steps.push(discName && state === "failed"
        ? row("01", "Disc", discName, "none", null, null)
        : row("01", "Disc", "your Gran Turismo 4 (USA) disc image", "active", null, null));
    }
    // A refused file, a stopped run and missing game files show the Disc row and the reason only, so the buttons
    // stay on screen at 1280 x 720.
    if (state === "needed" || running || done) {
      STEPS.forEach(function (step, index) {
        var status = "wait";
        var pct = null;
        var bytesText = null;
        var detail = step.detail;
        if (done || (running && index < order)) {
          status = "ok";
        } else if (running && index === order) {
          status = "active";
          pct = Math.round((setup.progress || 0) * 100);
          if (step.id === "copying" && setup.bytes_total) {
            bytesText = gigabytes(setup.bytes_done || 0) + " of " + gigabytes(setup.bytes_total);
          }
          if (step.id === "renderer") {
            detail = RENDERER_TEXT[setup.renderer_step] || detail;
            if (setup.renderer_step !== "downloading") {
              pct = null;
            } else if (setup.bytes_total) {
              bytesText = megabytes(setup.bytes_done || 0) + " of " + megabytes(setup.bytes_total);
            }
          }
        }
        if (done && step.id === "renderer" && setup.note) {
          detail = RENDERER_TEXT.failed;
        }
        steps.push(row("0" + (index + 2), step.label, detail, status, pct, bytesText));
      });
    }

    var problem = setup.problem || "";
    parts.problem.textContent = problem;
    parts.problem.hidden = !problem || running || done;
    parts.notice.textContent = setup.note || "";
    parts.notice.hidden = !(done && setup.note);

    if (done) {
      buttons.push({ label: "Finish", primary: true, run: finishSetup });
      hints.push({ key: "A", label: "Finish", run: finishSetup });
    } else if (running) {
      if (setup.step !== "renderer") {
        buttons.push({ label: "Cancel", run: cancelSetup });
        hints.push({ key: "B", label: "Cancel", run: cancelSetup });
      }
      buttons.push({ label: "Working…", primary: true, disabled: true });
    } else if (state === "failed") {
      if (discName) {
        buttons.push({ label: "Try again", run: startSetup });
      }
      buttons.push({ label: "Choose another file", primary: true, run: chooseDisc });
      hints.push({ key: "A", label: "Select" });
    } else {
      buttons.push({ label: state === "wrong_disc" ? "Choose another file" : "Choose disc image", primary: true, run: chooseDisc });
      hints.push({ key: "A", label: "Choose disc image", run: chooseDisc });
    }

    GT4.ui.clear(parts.steps);
    steps.forEach(function (node) {
      parts.steps.appendChild(node);
    });
    GT4.ui.clear(parts.foot);
    buttons.forEach(function (button) {
      parts.foot.appendChild(h("button", {
        class: "btn" + (button.primary ? " primary" : ""),
        type: "button",
        disabled: !!button.disabled,
        on: { click: function () { if (button.run) { button.run(); } } }
      }, button.label));
    });
    GT4.ui.setHints(hints);
    GT4.hooks.back = running && setup.step !== "renderer" ? cancelSetup : (done ? finishSetup : null);
  }

  var busyChoosing = false;

  // Rust opens the file picker and checks the file. A refusal comes back as the error and app_state then says
  // wrong_disc with the reason, so the page shows it in the problem box (no toast needed).
  function chooseDisc() {
    if (busyChoosing) {
      return;
    }
    busyChoosing = true;
    call("setup_choose_disc").then(function (info) {
      busyChoosing = false;
      if (info) {
        startSetup();
      } else {
        poll();
      }
    }, function () {
      busyChoosing = false;
      poll();
    });
  }

  function startSetup() {
    call("setup_start").then(function () {
      poll();
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
      poll();
    });
  }

  function cancelSetup() {
    call("setup_cancel").then(function () {
      poll();
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  // Setup is done: open Home. Play stays on Home; nothing starts the game here.
  function finishSetup() {
    GT4.go("#/home");
  }

  var active = null;

  function poll() {
    if (!active) {
      return;
    }
    call("app_state").then(function (appState) {
      if (!active) {
        return;
      }
      var setup = appState.setup || { state: "needed" };
      var sig = signature(setup);
      if (sig === active.last) {
        return;
      }
      var stateChanged = !active.lastState || active.lastState !== setup.state || active.lastStep !== setup.step;
      active.last = sig;
      active.lastState = setup.state;
      active.lastStep = setup.step;
      var focusedText = document.activeElement && active.root.contains(document.activeElement) ? document.activeElement.textContent : null;
      build(setup, active.parts);
      var buttons = Array.prototype.slice.call(active.parts.foot.querySelectorAll("button:not([disabled])"));
      var again = buttons.filter(function (b) { return b.textContent === focusedText; })[0];
      var primary = active.parts.foot.querySelector("button.primary:not([disabled])") || buttons[0];
      if (again && !stateChanged) {
        again.focus({ preventScroll: true });
      } else if (primary && (stateChanged || focusedText)) {
        primary.focus({ preventScroll: true });
      }
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  GT4.pages = GT4.pages || {};
  GT4.pages.setup = {
    mount: function (root) {
      var cover = h("img", { class: "cover", src: "assets/cover-front.png", alt: "Gran Turismo 4 cover" });
      var title = h("h1", { text: "" });
      var sub = h("p", { class: "sub", text: "" });
      var steps = h("ul", { class: "steps" });
      var problem = h("p", { class: "problem", role: "alert", hidden: true });
      var notice = h("p", { class: "notice", hidden: true });
      var foot = h("div", { class: "foot" });
      var parts = { title: title, sub: sub, steps: steps, problem: problem, notice: notice, foot: foot };
      var page = h("div", { class: "stage setup" }, [
        h("div", { class: "cover-col" }, [cover]),
        h("div", { class: "setup-main" }, [
          h("img", { class: "wordmark", src: "assets/logo-gt4-white.png", alt: "Gran Turismo 4" }),
          title,
          sub,
          steps,
          problem,
          notice,
          foot
        ])
      ]);
      root.appendChild(page);
      active = { root: page, parts: parts, last: null, lastState: null, lastStep: null };
      poll();
      var timer = setInterval(poll, POLL_MS);
      return {
        destroy: function () {
          active = null;
          clearInterval(timer);
        }
      };
    }
  };
})(window);
