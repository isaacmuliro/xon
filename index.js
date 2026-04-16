// JavaScript wrapper for the native Xon module
try {
    const xon = require('./build/Release/xon.node');

    const isIdentifier = (key) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(key);
    const toXon = (value, indent = 2, depth = 0) => {
        const pad = ' '.repeat(indent * depth);
        const childPad = ' '.repeat(indent * (depth + 1));

        if (value === null) return 'null';
        if (typeof value === 'boolean') return value ? 'true' : 'false';
        if (typeof value === 'number') return String(value);
        if (typeof value === 'string') return JSON.stringify(value);

        if (Array.isArray(value)) {
            if (value.length === 0) return '[]';
            return `[\n${value.map((item) => `${childPad}${toXon(item, indent, depth + 1)}`).join(',\n')}\n${pad}]`;
        }

        if (typeof value === 'object') {
            const entries = Object.entries(value);
            if (entries.length === 0) return '{}';
            return `{\n${entries
                .map(([key, item]) => {
                    const renderedKey = isIdentifier(key) ? key : JSON.stringify(key);
                    return `${childPad}${renderedKey}: ${toXon(item, indent, depth + 1)}`;
                })
                .join(',\n')}\n${pad}}`;
        }

        throw new TypeError(`Unsupported value type: ${typeof value}`);
    };
    
    module.exports = {
        // Branded API - xonify for parsing files
        xonify: xon.xonify,
        
        // Branded API - xonifyString for parsing strings
        xonifyString: xon.xonifyString,
        
        // Aliases for convenience
        parseFile: xon.xonify,
        parseString: xon.xonifyString,
        parse: xon.xonifyString,
        stringify: (obj, options = {}) => toXon(obj, options.indent ?? 2),
    };
} catch (err) {
    console.error('Failed to load Xon native module. Have you run `npm install`?');
    console.error(err.message);
    throw err;
}
