/*
 * UI feature flags. A flag set to false removes the feature from navigation
 * (tabs, LB/RB and Q/E cycling, routes) and hides its settings rows.
 * mods: the mod manager is release 1.1.0 (owner decision, 2026-10-16).
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  GT4.features = {
    mods: false
  };
  // The macOS app (WebKit web view): wording for the Trash and a renderer that ships inside the app.
  GT4.mac = /Macintosh/.test(root.navigator ? root.navigator.userAgent : "");
  // Home background slideshow (six slots, in order); see ui/assets/backgrounds/README.md. Each slot has a CSS
  // class bg-slide-1..6 in css/app.css, so the list length must match the CSS slots.
  GT4.config = {
    backgrounds: [
      "assets/backgrounds/bg-01.webp",
      "assets/backgrounds/bg-02.webp",
      "assets/backgrounds/bg-03.webp",
      "assets/backgrounds/bg-04.webp",
      "assets/backgrounds/bg-05.webp",
      "assets/backgrounds/bg-06.webp"
    ]
  };
})(window);
