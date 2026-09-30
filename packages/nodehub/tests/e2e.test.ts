import { describe, it, expect } from "vitest";
import { runCli } from "./commons";

describe("E2E Core Tests", () => {
  it("should run the CLI without errors using the default graph", async () => {
    // As per run-console.sh, the default graph file is in the same directory as the executable
    const args = ["--graph=./BlueprintsGraph.json"];
    
    const { stdout, stderr, code } = await runCli(args);

    // For now, we just expect the process to exit cleanly.
    // We can add assertions about stdout/stderr content later.
    expect(code).toBe(0);
    expect(stderr).toBe("");
  });
});
