> Create an **empty** repository named `skellytune-mini` under your GitHub account `skellyruno` first. Do not initialize it with a README, license, or .gitignore, since they are included here.

# Publish SkellyTune Mini to GitHub

## GitHub Desktop
1. Extract the ZIP.
2. GitHub Desktop → File → Add local repository; if prompted, create a repository in the extracted `skellytune-mini` directory.
3. Commit all files, including `.github/workflows/`.
4. Publish repository to GitHub.
5. From Actions, run **Build Windows VST3** and download the release artifact once the job turns green.

## Git command line

```bash
cd skell-vst-template-main
git init
git add .
git commit -m "Integrate SkellyTune v3.3 into JUCE VST3 template"
git branch -M main
git remote add origin https://github.com/skellyruno/skellytune-mini.git
git push -u origin main
```

Use a new empty GitHub repository for the commands above. If it already exists with commits, clone the repository and copy these files into it instead to avoid overwriting history.

## Troubleshooting
- If CMake fails to fetch JUCE, check GitHub runner network access or point `JUCE_PATH` to a checked-out JUCE 8.0.12.
- If no `.vst3` is packaged, inspect `Build Windows VST3` logs and confirm the Release build completed before the Package step.
- If the VST3 name isn't present in FL Studio, extract the bundle to `C:\Program Files\Common Files\VST3` and rescan your plugin paths.
- If your previous plugin used different IDs than v3.3, do not overwrite installed plugins before comparing their reported VST3 identifiers.
