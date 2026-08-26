/* GENERATED SOURCE for the LuauG documentation site. Copied verbatim by
   api/generator/gen_site.luau; edit it here, never in the generated output.

   No framework, no build step and no network. The site has to work as a folder
   on somebody's disk, which rules out `fetch` -- every browser blocks it from a
   file:// origin -- so the search index arrives as a script that assigns
   `window.LuaugSearchIndex` rather than as JSON that has to be fetched. */

(function () {
    "use strict";

    var root = (window.LuaugDocs && window.LuaugDocs.root) || "";

    /* ---------- theme ---------- */

    var THEME_KEY = "luaug-docs-theme";

    function applyTheme(value) {
        document.documentElement.setAttribute("data-theme", value);
    }

    function storedTheme() {
        try {
            return window.localStorage.getItem(THEME_KEY);
        } catch (error) {
            /* Private windows and blocked site data both throw on access
               rather than returning null, and a documentation page that fails
               to render because it could not remember a colour is worse than
               one that forgets. */
            return null;
        }
    }

    var initialTheme = storedTheme();
    if (initialTheme === "light" || initialTheme === "dark") {
        applyTheme(initialTheme);
    }

    var themeToggle = document.getElementById("theme-toggle");
    if (themeToggle) {
        themeToggle.addEventListener("click", function () {
            var current = document.documentElement.getAttribute("data-theme");
            var prefersDark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
            var resolved = current === "auto" ? (prefersDark ? "dark" : "light") : current;
            var next = resolved === "dark" ? "light" : "dark";
            applyTheme(next);
            try {
                window.localStorage.setItem(THEME_KEY, next);
            } catch (error) {
                /* Nothing to do: the page is correct either way. */
            }
        });
    }

    /* ---------- mobile navigation ---------- */

    var menuToggle = document.getElementById("menu-toggle");
    var sidebar = document.getElementById("sidebar");
    if (menuToggle && sidebar) {
        menuToggle.addEventListener("click", function () {
            var open = sidebar.classList.toggle("open");
            menuToggle.setAttribute("aria-expanded", open ? "true" : "false");
        });
    }

    /* ---------- on-this-page highlighting ---------- */

    var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc a"));
    if (tocLinks.length > 0 && "IntersectionObserver" in window) {
        var byId = {};
        tocLinks.forEach(function (link) {
            byId[link.getAttribute("href").slice(1)] = link;
        });

        var visible = {};
        var observer = new IntersectionObserver(
            function (entries) {
                entries.forEach(function (entry) {
                    visible[entry.target.id] = entry.isIntersecting;
                });
                var active = null;
                Object.keys(byId).forEach(function (id) {
                    if (visible[id] && active === null) {
                        active = id;
                    }
                });
                tocLinks.forEach(function (link) {
                    link.classList.remove("active");
                });
                if (active && byId[active]) {
                    byId[active].classList.add("active");
                }
            },
            { rootMargin: "-15% 0px -70% 0px" }
        );

        Object.keys(byId).forEach(function (id) {
            var target = document.getElementById(id);
            if (target) {
                observer.observe(target);
            }
        });
    }

    /* ---------- search ---------- */

    var input = document.getElementById("search-input");
    var results = document.getElementById("search-results");

    function scoreEntry(entry, needle) {
        var name = entry.n.toLowerCase();
        var index = name.indexOf(needle);
        if (index < 0) {
            /* A member is also findable by its bare name: somebody types
               "Raycast", not "Workspace.Raycast". */
            var bare = name.slice(name.lastIndexOf(".") + 1);
            var bareIndex = bare.indexOf(needle);
            if (bareIndex < 0) {
                return -1;
            }
            return 40 + bareIndex;
        }
        if (name === needle) {
            return 0;
        }
        if (index === 0) {
            return 10 + (name.length - needle.length) / 100;
        }
        return 25 + index;
    }

    function renderResults(matches, needle) {
        if (matches.length === 0) {
            results.innerHTML = '<p class="search-empty">Nothing matches “' + escapeHtml(needle) + "”.</p>";
            results.hidden = false;
            return;
        }
        var html = matches
            .map(function (entry, position) {
                var where = entry.o ? '<span class="where">' + escapeHtml(entry.o) + "</span>" : "";
                return (
                    '<a class="search-result' +
                    (position === 0 ? " selected" : "") +
                    '" href="' +
                    root +
                    escapeHtml(entry.p) +
                    '"><span class="kind">' +
                    escapeHtml(entry.k) +
                    '</span><span class="name">' +
                    escapeHtml(entry.n) +
                    "</span>" +
                    where +
                    "</a>"
                );
            })
            .join("");
        results.innerHTML = html;
        results.hidden = false;
    }

    function escapeHtml(text) {
        return String(text).replace(/[&<>"]/g, function (character) {
            return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[character];
        });
    }

    if (input && results) {
        var index = window.LuaugSearchIndex || [];

        var run = function () {
            var needle = input.value.trim().toLowerCase();
            if (needle.length < 2) {
                results.hidden = true;
                results.innerHTML = "";
                return;
            }
            var scored = [];
            for (var i = 0; i < index.length; i += 1) {
                var score = scoreEntry(index[i], needle);
                if (score >= 0) {
                    scored.push({ entry: index[i], score: score });
                }
            }
            scored.sort(function (a, b) {
                return a.score - b.score;
            });
            renderResults(
                scored.slice(0, 25).map(function (item) {
                    return item.entry;
                }),
                needle
            );
        };

        input.addEventListener("input", run);
        input.addEventListener("focus", run);

        document.addEventListener("click", function (event) {
            if (!results.contains(event.target) && event.target !== input) {
                results.hidden = true;
            }
        });

        input.addEventListener("keydown", function (event) {
            var items = Array.prototype.slice.call(results.querySelectorAll(".search-result"));
            if (items.length === 0) {
                return;
            }
            var current = items.findIndex(function (item) {
                return item.classList.contains("selected");
            });
            if (event.key === "ArrowDown" || event.key === "ArrowUp") {
                event.preventDefault();
                var next = event.key === "ArrowDown" ? current + 1 : current - 1;
                if (next < 0) {
                    next = items.length - 1;
                }
                if (next >= items.length) {
                    next = 0;
                }
                items.forEach(function (item) {
                    item.classList.remove("selected");
                });
                items[next].classList.add("selected");
                items[next].scrollIntoView({ block: "nearest" });
            } else if (event.key === "Enter") {
                event.preventDefault();
                var chosen = items[current >= 0 ? current : 0];
                if (chosen) {
                    window.location.href = chosen.getAttribute("href");
                }
            } else if (event.key === "Escape") {
                results.hidden = true;
                input.blur();
            }
        });

        document.addEventListener("keydown", function (event) {
            var typing =
                document.activeElement &&
                (document.activeElement.tagName === "INPUT" || document.activeElement.tagName === "TEXTAREA");
            if (typing) {
                return;
            }
            if (event.key === "/" || ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k")) {
                event.preventDefault();
                input.focus();
                input.select();
            }
        });
    }

    /* ---------- Luau highlighting ---------- */

    var KEYWORDS = {};
    (
        "and break do else elseif end false for function if in local nil not or repeat return then true until while " +
        "continue export type typeof self"
    )
        .split(" ")
        .forEach(function (word) {
            KEYWORDS[word] = true;
        });

    /* Tokenised in one left-to-right pass so that a keyword inside a string and
       a quote inside a comment are both inert -- the two mistakes every regex
       highlighter makes. */
    function highlightLuau(source) {
        var out = "";
        var i = 0;
        var length = source.length;

        while (i < length) {
            var character = source[i];
            var rest = source.slice(i);

            var longComment = /^--\[(=*)\[/.exec(rest);
            if (longComment) {
                var closer = "]" + longComment[1] + "]";
                var end = source.indexOf(closer, i);
                var stop = end < 0 ? length : end + closer.length;
                out += '<span class="tok-com">' + escapeHtml(source.slice(i, stop)) + "</span>";
                i = stop;
                continue;
            }

            if (character === "-" && source[i + 1] === "-") {
                var lineEnd = source.indexOf("\n", i);
                var commentStop = lineEnd < 0 ? length : lineEnd;
                out += '<span class="tok-com">' + escapeHtml(source.slice(i, commentStop)) + "</span>";
                i = commentStop;
                continue;
            }

            var longString = /^\[(=*)\[/.exec(rest);
            if (longString) {
                var stringCloser = "]" + longString[1] + "]";
                var stringEnd = source.indexOf(stringCloser, i);
                var stringStop = stringEnd < 0 ? length : stringEnd + stringCloser.length;
                out += '<span class="tok-str">' + escapeHtml(source.slice(i, stringStop)) + "</span>";
                i = stringStop;
                continue;
            }

            if (character === '"' || character === "'" || character === "`") {
                var j = i + 1;
                while (j < length) {
                    if (source[j] === "\\") {
                        j += 2;
                        continue;
                    }
                    if (source[j] === character || source[j] === "\n") {
                        break;
                    }
                    j += 1;
                }
                out += '<span class="tok-str">' + escapeHtml(source.slice(i, Math.min(j + 1, length))) + "</span>";
                i = j + 1;
                continue;
            }

            var number = /^0[xX][0-9a-fA-F_]+|^\d[\d_]*\.?[\d_]*([eE][+-]?\d+)?/.exec(rest);
            if (number && /[\d]/.test(character)) {
                out += '<span class="tok-num">' + escapeHtml(number[0]) + "</span>";
                i += number[0].length;
                continue;
            }

            var word = /^[A-Za-z_][A-Za-z0-9_]*/.exec(rest);
            if (word) {
                var text = word[0];
                var after = rest.slice(text.length);
                if (KEYWORDS[text]) {
                    out += '<span class="tok-kw">' + text + "</span>";
                } else if (/^\s*\(/.test(after)) {
                    out += '<span class="tok-fn">' + text + "</span>";
                } else if (/^[A-Z]/.test(text)) {
                    out += '<span class="tok-type">' + text + "</span>";
                } else {
                    out += escapeHtml(text);
                }
                i += text.length;
                continue;
            }

            out += escapeHtml(character);
            i += 1;
        }

        return out;
    }

    Array.prototype.slice.call(document.querySelectorAll("pre > code.language-luau")).forEach(function (block) {
        block.innerHTML = highlightLuau(block.textContent);
    });
})();
