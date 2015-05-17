'use strict';

/**
 * @file Command-line utility that bundles the static (frontend) files into a JSON file.
 * The output file is then used by the backend to serve the static files from memory.
 *
 * The output JSON file has the following structure:
 * { "bundle": { "files": [ { "path": "<relative-path>", "content": "<base64-of-content>" } ] } }
 *
 * Note that the content is base64-encoded to correctly pass binary data through JSON.
 *
 * @param {string} dirInitialPath The path to the directory with the static files to scan.
 * @param {string} [outputFilePath] If omitted or equals to '-', flushes to stdout.
 * @return {string} The JSON bundle.
 */

var _fs = require('fs');
var _path = require('path');


if (process.argc < 1) {
  process.stderr.write('Usage: node ' + _path.basename(__filename) + ' <path-to-static-src-dir> [path-to-output-file]\n');
  process.exit(-1);
  return;
}

// Get the options from the command line.
// Format: `node build-static.js <path-to-static-src-dir> [path-to-output-file]`.
var processArgv = process.argv.slice(2);
var dirInitialPath = processArgv[0];
var outputFilePath = processArgv[1];

// The recursive scan is FIFO, one directory at a time, using sync I/O.
var queue = [ dirInitialPath ];
var files = [];

scanContinue();

/**
 * Scans one directory, queues the directories found in it.
 *
 * @param {string} dirPath The directory path to scan.
 */
function scan(dirPath) {
  try {
    var dirRelativePath = _path.relative(dirInitialPath, dirPath);
    dirRelativePath = dirRelativePath.replace(/^\.[/\\]/);
    
    var fileNames = _fs.readdirSync(dirPath);
    
    fileNames.forEach(function (fileName) {
      var filePath = _path.join(dirPath, fileName);
      
      if (_fs.statSync(filePath).isDirectory()) {
        queue.push(filePath);
      }
      else if (
        // Not UNIX-hidden files ...
        !/^\./.test(fileName)
        // ... and has a non-ignored extension
        // (do not use whitelist to not forget some important filetype e.g. `.woff2`).
        && [
          '',
          '.txt',
          '.md',
          '.eot'
        ].indexOf(_path.extname(fileName).toLowerCase()) < 0
      ) {
        files.push({
          path: _path.join(dirRelativePath, fileName),
          content: _fs.readFileSync(filePath).toString('base64')
        });
      }
    });
    
    scanContinue();
  }
  catch (err) {
    scanDone(err);
  }
}

/**
 * Schedules a scan of the next directory from the queue.
 */
function scanContinue() {
  if (queue.length) {
    process.nextTick(function () {
      scan(queue.shift());
    });
  }
  else {
    scanDone();
  }
}

/**
 * Handles scan completion, both success and failure.
 *
 * @param {Error} [err] The error during the scan, if present.
 */
function scanDone(err) {
  if (err) {
    process.stderr.write((err.stack || err) + '\n');
    process.exit(-2);
    return;
  }
  
  var outputObject = {
    "bundle": {
      "files": files
    }
  };
  var outputJson = JSON.stringify(outputObject, true, 2);
  
  // If the file path is specified, populate the file.
  if (outputFilePath && outputFilePath !== '-') {
    try {
      _fs.writeFileSync(outputFilePath, outputJson, { flag: 'w+' });
    }
    catch (err) {
      process.stderr.write((err.stack || err) + '\n');
      process.exit(-3);
      return;
    }
  }
  else {
    // Otherwise, flush to stdout.
    process.stdout.write(outputJson + '\n');
  }
  
  process.exit(0);
  return;
}
