# FL Studio saved-settings crash recovery

The reported error is "Fruity Wrapper - SkellyTune Realtime plugin crashed while loading its settings." A screenshot alone doesn't establish the cause; invalid saved state, duplicate installations or another plugin bug can all contribute.

1. Close FL Studio without overwriting the affected FLP. Back up the project and its last known-good backup.
2. Install the newly built VST3 after backing up the old version. Avoid duplicate builds with the same VST3 identifier on different scan paths.
3. Test SkellyTune in a new empty FL Studio project first.
4. Open **a copy** of the affected FLP and check the saved instance. If only that instance fails, replace it in a copy and reconstruct its settings if necessary.
5. Capture an FL Studio crash report/log and the affected project's saved state to identify the underlying fault.

The v3.3.1 source verifies host chunk size and XML type, filters to 16 stable parameter IDs, strictly parses finite numeric values and clamps legacy ranges. The portable test suite covers this schema, but it cannot simulate the Windows Fruity Wrapper. Preserve preset compatibility and run the host check before claiming the crash is fixed.
