import { execFile } from "child_process";
import { promisify } from "util";
import * as path from "path";

const execFileAsync = promisify(execFile);

// Define the path to the binary directory, which will be our working directory.
const binDirectory = path.resolve(__dirname, "../build/bin");

// Define the path to the executable.
const executablePath = path.resolve(binDirectory, "nodehub_d.exe");

// Define the common arguments for running the CLI in a headless test environment
const commonArgs = [
    "--headless",
    "--run",
    "--log-level=info",
    "--max-steps=1000"
];

/**
 * Runs the CLI application with a given set of arguments.
 *
 * @param testArgs - An array of strings representing the arguments specific to the test.
 * @returns A promise that resolves with the stdout, stderr, and exit code of the process.
 */
export async function runCli(testArgs: string[]) {
  const allArgs = [...commonArgs, ...testArgs];

  try {
    const { stdout, stderr } = await execFileAsync(executablePath, allArgs, {
        cwd: binDirectory // Run from the binary's directory
    });
    return { stdout, stderr, code: 0 };
  } catch (error: any) {
    // If execFileAsync throws, it's because the process exited with a non-zero code.
    // We still want to capture the output and the code.
    return {
      stdout: error.stdout,
      stderr: error.stderr,
      code: error.code || 1,
    };
  }
}
