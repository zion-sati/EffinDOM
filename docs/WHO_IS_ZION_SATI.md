# Who is zion-sati?

I've spent four decades in software. I've written Linux kernel drivers at the
bare-metal level, built low-latency search engines where every microsecond
counted, and shipped web applications to millions of users.

I earned my first $1,000 when I was 14, selling a GUI Editbox control to a friend. The GUI framework ran in VGA 640×480 at 256 colors — long before Windows made this easy. The problem: VGA's memory aperture was only 64 KB wide, but 640×480×256 colors is 300 KB of pixel data. So you couldn't just write to video memory linearly. You had to program the VGA sequencer registers to switch between four 64 KB banks — bank 0 for scanlines 0–119, bank 1 for 120–239, bank 2 for 240–359, bank 3 for 360–479. Every time you crossed a bank boundary while drawing a line or filling a rectangle, you had to switch banks mid-operation. If you got it wrong, you'd write pixels into the wrong part of the screen and your GUI would look like a glitch-art masterpiece. I got it right. Sold it. That was the moment I knew I'd be doing this forever.

I've seen abstractions come and go, paradigms rise and fall, and the same mistakes
repackaged as innovation every five years.

**zion-sati is a pseudonym.** It's from The Matrix — my favorite film, and the
only one that ever got our industry right.

**Zion** is the last human city, buried deep underground, fighting a war
against machines that have already won. That's where we are. The DOM won.
JavaScript won. We're huddled in frameworks, patching cracks in a 30-year-old
document viewer, pretending it's an application platform. Every brown-bag talk
about the next state-management library is another trench dug deeper into a
foundation that was never meant to hold weight.

**Sati** is the little girl at the end of Revolutions — a program without
purpose, created by two programs in love. She represents something new.
Something that doesn't belong to the war between humans and machines, between
frameworks and the DOM, between the old world and its assumptions. She makes
the sunrise. She changes the rules.

That's what EffinDom is. Not another framework. Not another trench. A new
world. Built by someone who spent 40 years watching the old one fail, and
finally decided to do something about it.

I'm a solo engineer. I have a young family. I build this at night, on
weekends, in the gaps between life. There is no team, no funding. I use AI as an accelerator — it compresses the
grunt work, but the architecture, the eight years of failed experiments, the
lessons about what breaks when you try to put a UI framework on a canvas —
those were earned the hard way. AI didn't teach me why naive canvas frameworks
fail. Burning through them did. Just four decades of knowing what good software
feels like, and
the stubborn belief that the web deserves better than a prophylactic that
hasn't fit right since 1995.
