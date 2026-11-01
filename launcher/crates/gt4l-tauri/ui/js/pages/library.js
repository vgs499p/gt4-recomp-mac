/*
 * Saves and Replays tabs. One list component, two commands sets:
 *   saves:   saves_list, saves_import, saves_activate({id}), saves_delete({id})
 *   replays: replays_list, replays_import, replays_delete({id})
 * Item = {id (library name, sent back to activate/delete), name (shown), modified (ISO 8601), active (saves only)}. The list result also has `folder`: the
 * user data folder that holds the files (%APPDATA%\GT4Recomp\Saves or \Replays), shown with
 * "Open folder" (open_saves_folder / open_replays_folder).
 * saves_import / replays_import return the new list, or null when cancelled.
 *
 * Keys on a focused row: A = select (saves: make active), Y = make active
 * (saves) or delete (replays). X = import. Delete also has a trash button.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  var KINDS = {
    saves: {
      title: "Saves",
      note: "The selected save is used the next time you play.",
      importLabel: "Import save",
      itemWord: "Save",
      empty: { title: "No saves yet", text: "Your first save appears here after you save in the game." },
      list: "saves_list",
      importCmd: "saves_import",
      activate: "saves_activate",
      remove: "saves_delete",
      removeTitle: "Delete save?",
      removeText: GT4.mac ? "The save goes to the Trash. You can restore it from there." : "The save goes to the Recycle Bin. You can restore it from there."
    },
    replays: {
      title: "Replays",
      note: "All replays are kept. Replays you save in the game appear here.",
      importLabel: "Import replays",
      itemWord: "Replay",
      empty: { title: "No replays yet", text: "Replays you save in the game appear here." },
      list: "replays_list",
      importCmd: "replays_import",
      activate: null,
      remove: "replays_delete",
      removeTitle: "Delete replay?",
      removeText: GT4.mac ? "The replay goes to the Trash. You can restore it from there." : "The replay goes to the Recycle Bin. You can restore it from there."
    }
  };

  function mountLibrary(content, host, kindName) {
    var kind = KINDS[kindName];
    var items = [];
    var list = h("div", { class: "lib-list" });
    var headNote = h("p", { class: "note", text: kind.note });
    var folderText = h("span", { class: "val lib-folder-path", text: "" });

    function openFolder() {
      var request = kindName === "saves" ? GT4.backend.call("open_saves_folder", {}) : GT4.backend.call("open_replays_folder", {});
      request.catch(function (err) {
        GT4.ui.toast(GT4.errorText(err), true);
      });
    }

    function selectedRow() {
      var active = document.activeElement;
      return active && active.dataset && active.dataset.itemId ? active : null;
    }

    function removeItem(id) {
      GT4.ui.openDialog({
        title: kind.removeTitle,
        text: kind.removeText,
        buttons: [
          { label: "Cancel" },
          {
            label: "Delete",
            primary: true,
            danger: true,
            run: function () {
              GT4.backend.call(kind.remove, { id: id }).then(function (result) {
                apply(result, "Deleted");
              }, function (err) {
                GT4.ui.toast(GT4.errorText(err), true);
              });
            }
          }
        ]
      });
    }

    function makeActive(id) {
      if (!kind.activate) {
        return;
      }
      GT4.backend.call(kind.activate, { id: id }).then(function (result) {
        apply(result, "Next play uses this save");
      }, function (err) {
        GT4.ui.toast(GT4.errorText(err), true);
      });
    }

    function row(item, index) {
      var children = [];
      if (kindName === "saves") {
        children.push(h("span", { class: "radio", "aria-hidden": "true" }));
      } else {
        children.push(h("span"));
      }
      // Row number: the position in the list (the id is the library name, e.g. "Save 02").
      children.push(h("span", { class: "no", text: String(index + 1).padStart(2, "0") }));
      var nameNode = h("span", { class: "nm", text: item.name || kind.itemWord + " " + item.id });
      if (item.active) {
        nameNode.appendChild(h("small", { text: "Next play" }));
      }
      children.push(nameNode);
      children.push(h("span", { class: "date", text: GT4.ui.formatDate(item.modified) }));
      children.push(h("span", { class: "acts" }, [
        h("button", {
          class: "iconbtn del",
          type: "button",
          "aria-label": "Delete " + (item.name || item.id),
          title: "Delete",
          on: {
            click: function (event) {
              event.stopPropagation();
              removeItem(item.id);
            }
          }
        }, trashIcon())
      ]));

      var node = h("div", {
        class: "slot-row" + (item.active ? " active" : ""),
        role: kindName === "saves" ? "radio" : "listitem",
        "aria-checked": kindName === "saves" ? String(!!item.active) : null,
        tabindex: "0",
        data: { focusable: "1", itemId: item.id },
        on: {
          click: function () {
            if (kind.activate) {
              makeActive(item.id);
            }
          }
        }
      }, children);
      return node;
    }

    function trashIcon() {
      var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
      svg.setAttribute("width", "18");
      svg.setAttribute("height", "18");
      svg.setAttribute("viewBox", "0 0 24 24");
      svg.setAttribute("fill", "none");
      svg.setAttribute("stroke", "currentColor");
      svg.setAttribute("stroke-width", "1.6");
      svg.setAttribute("stroke-linecap", "round");
      svg.setAttribute("aria-hidden", "true");
      var path = document.createElementNS("http://www.w3.org/2000/svg", "path");
      path.setAttribute("d", "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13");
      svg.appendChild(path);
      return svg;
    }

    function paint(focusId) {
      GT4.ui.clear(list);
      if (!items.length) {
        list.classList.remove("lib-list");
        list.appendChild(h("div", { class: "emptystate" }, [
          h("h3", { text: kind.empty.title }),
          h("p", { text: kind.empty.text })
        ]));
      } else {
        list.classList.add("lib-list");
        items.forEach(function (item, index) {
          list.appendChild(row(item, index));
        });
      }
      if (focusId !== undefined) {
        var target = list.querySelector("[data-item-id=\"" + focusId + "\"]");
        if (target) {
          target.focus({ preventScroll: true });
        }
      }
    }

    function apply(result, message) {
      if (result && Array.isArray(result.items)) {
        items = result.items;
      }
      if (result && typeof result.folder === "string" && result.folder) {
        folderText.textContent = result.folder;
      }
      if (message) {
        GT4.ui.toast(message);
      }
      paint();
    }

    function importItems() {
      GT4.backend.call(kind.importCmd, {}).then(function (result) {
        if (!result) {
          return;
        }
        apply(result, "Import finished");
      }, function (err) {
        GT4.ui.toast(GT4.errorText(err), true);
      });
    }

    var importButton = h("button", {
      class: "btn primary",
      type: "button",
      on: { click: importItems }
    }, kind.importLabel);

    content.appendChild(h("div", { class: "lib-head" }, [
      h("div", null, [h("h1", { text: kind.title }), headNote]),
      importButton
    ]));
    content.appendChild(list);
    // Where the files are: the user data folder (%APPDATA%\GT4Recomp), easy to open and back up.
    content.appendChild(h("div", { class: "row lib-folder" }, [
      h("div", { class: "k" }, [h("span", { text: "Folder" }), folderText]),
      h("div", { class: "ctl" }, [
        h("button", { class: "btn small", type: "button", on: { click: openFolder } }, "Open folder")
      ])
    ]));

    host.setHooks({
      x: importItems,
      y: function () {
        var row = selectedRow();
        if (!row) {
          GT4.ui.toast("Select an item first", true);
          return;
        }
        var id = row.dataset.itemId;
        if (kind.activate) {
          makeActive(id);
        } else {
          removeItem(id);
        }
      }
    });
    // A enters the list on its first row; Import is the entry only when the list is empty.
    host.entry(function () {
      var rows = list.querySelectorAll("[data-item-id]");
      return GT4.settingsModel.libraryEntry(rows.length) === "row" ? rows[0] : importButton;
    });
    host.hints([
      { key: "X", label: "Import", run: importItems },
      { key: "Y", label: kind.activate ? "Make active" : "Delete", run: function () { if (GT4.hooks.y) { GT4.hooks.y(); } } }
    ]);

    content.addEventListener("keydown", function (event) {
      if (event.key === "Delete") {
        var row = selectedRow();
        if (row) {
          removeItem(row.dataset.itemId);
        }
      }
    });

    // Drawing the list never moves focus: the tab list keeps it until the player presses A (host.entry).
    return GT4.backend.call(kind.list, {}).then(function (result) {
      apply(result, "");
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  GT4.tabPages.saves = function (content, host) {
    return mountLibrary(content, host, "saves");
  };
  GT4.tabPages.replays = function (content, host) {
    return mountLibrary(content, host, "replays");
  };
})(window);
