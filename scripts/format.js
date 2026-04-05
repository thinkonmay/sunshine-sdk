const { execSync } = require('child_process');
const glob = require('glob');
const path = require('path');

const patterns = ['src/**/*.c', 'src/**/*.h', 'src/**/*.cpp', 'src/**/*.hpp'];
const files = patterns.flatMap((pattern) => glob.sync(pattern));

if (files.length === 0) {
  console.log('No C/H files found to format.');
  process.exit(0);
}

console.log(`Formatting ${files.length} files...`);

// To avoid command line length limits on Windows, we'll format in batches or one by one
// Given the project size, one by one is safe and simple, or we can use batches of 50.
const batchSize = 50;
for (let i = 0; i < files.length; i += batchSize) {
  const batch = files.slice(i, i + batchSize);
  const command = `npx clang-format -i ${batch.map((f) => `"${f}"`).join(' ')}`;
  try {
    execSync(command, { stdio: 'inherit' });
  } catch (error) {
    console.error('Error formatting files:', error);
    process.exit(1);
  }
}

console.log('C/H formatting complete.');
