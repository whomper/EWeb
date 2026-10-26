/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* a script over EWeb's JS_KEEP_MIN (4 KB), kept compiled: each of what a
   script's bytecode carries (tests/js/kept.py; tests/js/bc68k.py reads the
   Pi's bytecode on the 68030) - regular expressions, wide strings, numbers,
   templates, classes, closures, generators, getters, labels and switches */
var kept = (function () {
	"use strict";
	var out = [];
	function say(what, v) { out.push(what + "=" + v); }

	/* regular expressions: their own bytecode, a string in the script's */
	var words = "the quick brown fox jumps over the lazy dog".match(/\b\w{5}\b/g);
	say("re1", words.join(","));
	say("re2", /^(\d{4})-(\d{2})-(\d{2})$/.exec("2026-10-26").slice(1).join("/"));
	say("re3", "a1b22c333".replace(/(\d+)/g, function (m) { return "<" + m.length + ">"; }));
	say("re4", /[א-ת]+/.test("שלום") + "," + /[א-ת]+/.test("hello"));
	say("re5", "x".repeat(40).replace(/x{3,7}?/g, "y").length);
	say("re6", /(?<year>\d{4})/.exec("in 1982").groups.year);
	say("re7", "Mixed Case".replace(/[A-Z]/gu, function (c) { return c.toLowerCase(); }));
	say("re8", /^\p{Lu}/u.test("Élan") + "," + /a.c/s.test("a\nc"));
	say("re9", "a,b;c d".split(/[,; ]/).length);
	say("re10", /(a|ab)(c|bcd)(d*)/.exec("abcd").join("|"));

	/* strings: wide ones (UTF-16 in the bytecode), escapes */
	var heb = "שלום עולם";
	say("s1", heb.length + "," + heb.charCodeAt(0).toString(16));
	say("s2", "éèê".normalize("NFD").length);
	say("s3", "Ω≈ç√∫".split("").reverse().join(""));
	say("s4", "😀".codePointAt(0).toString(16));
	say("s5", `${heb.slice(0, 4)}|${1 + 2}|${"a".padStart(3, "-")}`);

	/* numbers: doubles, big and small, as the bytecode keeps them */
	say("n1", (0.1 + 0.2).toFixed(17));
	say("n2", 1e300 * 10);
	say("n3", Math.round(Math.PI * 1e6));
	say("n4", (-0).toString() + "," + Object.is(-0, 0));
	say("n5", 0x7fffffff + 1);
	say("n6", 9007199254740993);
	say("n7", parseFloat("3.14159e2"));
	say("n8", (255).toString(2) + "," + (1 / 3).toPrecision(5));
	say("n9", [5, 1, 10, 2].sort(function (a, b) { return a - b; }).join(" "));

	/* classes, getters, closures, generators */
	class Shape {
		constructor(w, h) { this.w = w; this.h = h; }
		get area() { return this.w * this.h; }
		static of(n) { return new Shape(n, n); }
	}
	class Box extends Shape {
		constructor(w, h, d) { super(w, h); this.d = d; }
		get area() { return super.area * this.d; }
		toString() { return "Box(" + this.w + "x" + this.h + "x" + this.d + ")"; }
	}
	say("c1", Shape.of(7).area + "," + new Box(2, 3, 4).area + "," + new Box(1, 1, 1));
	function counter() { var n = 0; return function () { return ++n; }; }
	var c = counter(); c(); c();
	say("c2", c());
	function* fib() { var a = 0, b = 1; for (;;) { yield a; var t = a + b; a = b; b = t; } }
	var f = [], g = fib();
	for (var i = 0; i < 12; i++) f.push(g.next().value);
	say("c3", f.join(","));
	var o = { a: 1, get b() { return this.a + 1; }, ["c" + "d"]: 3, "שם": "x" };
	say("c4", JSON.stringify(o) + "," + Object.keys(o).length);
	var [p, , q = 9, ...rest] = [1, 2, undefined, 4, 5];
	say("c5", p + "," + q + "," + rest.join("+"));
	var { a: aa, cd: cc, ...others } = o;
	say("c6", aa + "," + cc + "," + Object.keys(others).join("+"));

	/* control flow: labels, switches, exceptions */
	var found = -1;
	outer: for (var r = 0; r < 5; r++)
		for (var s = 0; s < 5; s++)
			if (r * s === 6) { found = r * 10 + s; break outer; }
	say("f1", found);
	function kind(v) {
		switch (typeof v) {
		case "number": return v % 2 ? "odd" : "even";
		case "string": return "text";
		default: return "other";
		}
	}
	say("f2", [3, 4, "x", null].map(kind).join(","));
	try { null.x; } catch (e) { say("f3", e instanceof TypeError); }
	finally { say("f4", "finally"); }
	say("f5", [1, 2, 3, 4, 5, 6].filter(function (x) { return x % 2 === 0; })
		.reduce(function (a, b) { return a + b; }, 0));
	var m = new Map([["k", 1]]), st = new Set([1, 2, 2, 3]);
	say("f6", m.get("k") + "," + st.size);
	say("f7", typeof Symbol.iterator + "," + Array.from({ length: 3 }, function (_, i) { return i * i; }));
	return out.join(" ");
})();
console.log("kept " + kept);
