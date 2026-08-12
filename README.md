A notebook DIRECTLY in your DAW, that records ideas and tasks for the active song, from the prototype to the finished release, so that you always have an overview.

DAW Notes helps you take quick notes during the first hearing “Session All” close, if your ears still hear the song “neu” experience. You will always receive information about this, what your basic idea looked like, you should get lost.

You record tasks in the release to-dos, which you typically do AFTER the finished song: Release-Datum, Submit to distributor, Create a video for TikTok or YouTube. All of this can be set as a permanent task, so that every additional song already has these entries preset.
It's just your job, to process these points and the status “Completed” to set.

Last but not least, you can also have your song to-do sheet printed out as HTML for your home pinboard, to be able to do the last things manually, without looking into the PC.

DAW Notes does not access any data from the DAW and is located locally on your studio PC, is absolutely free and intended as a little helper for me and you.

## Building from source

This is a [JUCE](https://juce.com/) plugin project (VST3 + Standalone), managed via `DAW Notes.jucer`.

1. Clone or download [JUCE](https://github.com/juce-framework/JUCE) somewhere on your machine.
2. Open `DAW Notes.jucer` in the Projucer and set the JUCE modules path (either the project's global path setting, or per-module in the Modules section) to point at your JUCE checkout.
3. Save the project in the Projucer to (re-)generate `JuceLibraryCode/` and the `Builds/` exporter project for your IDE.
4. Build `Builds/VisualStudio2026/DAW Notes.sln` (or the exporter for your platform) as usual.

`Source/` contains all plugin code, including a vendored copy of [SQLite](https://sqlite.org/) (`sqlite3.c`/`.h`) - no separate database dependency needed.
