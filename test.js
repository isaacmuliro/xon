// Test file for Node.js bindings
const xon = require('./index');

console.log('=== Testing Xon Node.js Bindings ===\n');

try {
    // Test 1: Parse from file using branded xonify()
    console.log('Test 1: Parsing file with xonify()...');
    const config = xon.xonify('examples/config.xon');
    console.log('✅ Parsed successfully!');
    console.log('   App Name:', config.app_name);
    console.log('   Version:', config.version);
    console.log('   Debug:', config.debug);
    console.log('   Server Port:', config.server.port);
    
    // Test 2: Parse from string using branded xonifyString()
    console.log('\nTest 2: Parsing string with xonifyString()...');
    const data = xon.xonifyString(`{
        name: "Test App",
        count: 0x10,
        active: true,
        tags: ["fast", "simple",]
    }`);
    console.log('✅ Parsed successfully!');
    console.log('   Name:', data.name);
    console.log('   Count (hex):', data.count);
    console.log('   Active:', data.active);
    console.log('   Tags:', data.tags);

    // Test 3: stringify()
    console.log('\nTest 3: Serializing object with stringify()...');
    const xonText = xon.stringify({
        app_name: 'NodeBinding',
        features: ['parse', 'stringify'],
        enabled: true
    }, { indent: 2 });
    console.log('✅ Stringified successfully!');
    console.log(xonText);
    
    console.log('\n✅ All tests passed!');
} catch (err) {
    console.error('❌ Test failed:', err.message);
    process.exit(1);
}
