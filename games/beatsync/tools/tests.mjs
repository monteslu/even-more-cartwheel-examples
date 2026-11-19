// Every unit test of the song tooling, so `node --test tools/` runs them (Node resolves the directory to this file
// through tools/package.json "main"). `node --test 'tools/*.test.mjs'` runs the same files directly.
import './dsp.test.mjs';
import './chart.test.mjs';
