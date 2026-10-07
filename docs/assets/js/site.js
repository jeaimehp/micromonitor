// µMonitor site: mobile nav toggle and the language picker.
(function () {
  "use strict";

  // ---- Mobile nav ----
  var toggle = document.querySelector(".nav-toggle");
  var nav = document.getElementById("site-nav");
  if (toggle && nav) {
    toggle.addEventListener("click", function () {
      var open = toggle.getAttribute("aria-expanded") !== "true";
      toggle.setAttribute("aria-expanded", String(open));
      nav.classList.toggle("open", open);
    });
  }

  // ---- Language picker ----
  // English is the page language. Another choice is stored in Google Translate's "googtrans" cookie and the page
  // reloads; Google's script is loaded only when that cookie names a non-English language.
  var LANGS = ["es", "fr", "de", "pt", "ja"];
  var select = document.getElementById("lang-select");

  function currentLang() {
    var m = document.cookie.match(/(?:^|;\s*)googtrans=\/en\/([a-zA-Z-]+)/);
    return m && LANGS.indexOf(m[1]) >= 0 ? m[1] : "en";
  }

  function setLang(code) {
    var host = location.hostname;
    var domains = ["", "; domain=" + host, "; domain=." + host];
    domains.forEach(function (d) {
      document.cookie = "googtrans=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/" + d;
    });
    if (code !== "en") document.cookie = "googtrans=/en/" + code + "; path=/; max-age=31536000; SameSite=Lax";
    location.reload();
  }

  var lang = currentLang();
  if (select) {
    select.value = lang;
    select.addEventListener("change", function () { setLang(select.value); });
  }
  document.querySelectorAll("[data-lang]").forEach(function (a) {
    a.addEventListener("click", function (e) { e.preventDefault(); setLang(a.getAttribute("data-lang")); });
  });

  if (lang === "en") return;

  // Keep code, commands and file names in English.
  document.querySelectorAll("pre, code, kbd").forEach(function (el) {
    el.classList.add("notranslate");
    el.setAttribute("translate", "no");
  });
  var note = document.querySelector(".translate-note");
  if (note) note.hidden = false;

  window.googleTranslateElementInit = function () {
    new google.translate.TranslateElement(
      { pageLanguage: "en", includedLanguages: LANGS.join(","), autoDisplay: false },
      "google_translate_element"
    );
  };
  var s = document.createElement("script");
  s.src = "https://translate.google.com/translate_a/element.js?cb=googleTranslateElementInit";
  s.async = true;
  document.head.appendChild(s);
})();
