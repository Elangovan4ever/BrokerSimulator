/**
 * Schema comparison utilities for API response validation
 * Compares field names and types between Polygon API and Simulator responses
 */

export interface SchemaNode {
  type: string;
  fields?: Record<string, SchemaNode>;
  arrayElementType?: SchemaNode;
}

export interface SchemaComparisonResult {
  match: boolean;
  differences: SchemaDifference[];
}

export interface SchemaDifference {
  path: string;
  expected: string;
  actual: string;
  message: string;
}

/**
 * Get the type of a value including array element types
 */
function getType(value: unknown): string {
  if (value === null) return 'null';
  if (value === undefined) return 'undefined';
  if (Array.isArray(value)) return 'array';
  return typeof value;
}

/**
 * Extract schema from a JSON object
 * Recursively builds a schema tree representing the structure
 */
export function extractSchema(obj: unknown, maxArraySamples = 3): SchemaNode {
  const type = getType(obj);

  if (type === 'object' && obj !== null) {
    const fields: Record<string, SchemaNode> = {};
    for (const [key, value] of Object.entries(obj as Record<string, unknown>)) {
      fields[key] = extractSchema(value, maxArraySamples);
    }
    return { type: 'object', fields };
  }

  if (type === 'array') {
    const arr = obj as unknown[];
    if (arr.length === 0) {
      return { type: 'array', arrayElementType: { type: 'unknown' } };
    }
    // Sample first few elements to determine array element schema
    const samples = arr.slice(0, maxArraySamples);
    const mergedSchema = mergeSchemas(samples.map(item => extractSchema(item, maxArraySamples)));
    return { type: 'array', arrayElementType: mergedSchema };
  }

  return { type };
}

/**
 * Merge multiple schemas into one (for array element type detection)
 */
function mergeSchemas(schemas: SchemaNode[]): SchemaNode {
  if (schemas.length === 0) return { type: 'unknown' };
  if (schemas.length === 1) return schemas[0];

  // Check if all schemas have the same type
  const types = new Set(schemas.map(s => s.type));

  if (types.size > 1) {
    // Mixed types - return union type
    return { type: Array.from(types).sort().join('|') };
  }

  const type = schemas[0].type;

  if (type === 'object') {
    // Merge object fields from all schemas
    const allFields = new Map<string, SchemaNode[]>();
    for (const schema of schemas) {
      if (schema.fields) {
        for (const [key, value] of Object.entries(schema.fields)) {
          if (!allFields.has(key)) allFields.set(key, []);
          allFields.get(key)!.push(value);
        }
      }
    }

    const mergedFields: Record<string, SchemaNode> = {};
    for (const [key, fieldSchemas] of allFields) {
      mergedFields[key] = mergeSchemas(fieldSchemas);
    }
    return { type: 'object', fields: mergedFields };
  }

  if (type === 'array') {
    const elementSchemas = schemas
      .filter(s => s.arrayElementType)
      .map(s => s.arrayElementType!);
    return {
      type: 'array',
      arrayElementType: mergeSchemas(elementSchemas),
    };
  }

  return { type };
}

/**
 * Compare two schemas and return differences
 */
export function compareSchemas(
  expected: SchemaNode,
  actual: SchemaNode,
  path = ''
): SchemaComparisonResult {
  const differences: SchemaDifference[] = [];

  // Handle type mismatches
  if (expected.type !== actual.type) {
    // Allow some type flexibility (null can match with other types)
    const expectedTypes = expected.type.split('|');
    const actualTypes = actual.type.split('|');
    const hasOverlap = expectedTypes.some(t => actualTypes.includes(t)) ||
                       expectedTypes.includes('null') || actualTypes.includes('null');

    if (!hasOverlap) {
      differences.push({
        path: path || 'root',
        expected: expected.type,
        actual: actual.type,
        message: `Type mismatch: expected ${expected.type}, got ${actual.type}`,
      });
    }
  }

  // Compare object fields
  if (expected.type === 'object' && actual.type === 'object') {
    const expectedFields = expected.fields || {};
    const actualFields = actual.fields || {};

    // Check for missing fields in actual
    for (const key of Object.keys(expectedFields)) {
      if (!(key in actualFields)) {
        differences.push({
          path: path ? `${path}.${key}` : key,
          expected: 'present',
          actual: 'missing',
          message: `Missing field: ${key}`,
        });
      } else {
        // Recursively compare nested schemas
        const nestedResult = compareSchemas(
          expectedFields[key],
          actualFields[key],
          path ? `${path}.${key}` : key
        );
        differences.push(...nestedResult.differences);
      }
    }

    // Check for extra fields in actual (optional - log as info)
    for (const key of Object.keys(actualFields)) {
      if (!(key in expectedFields)) {
        differences.push({
          path: path ? `${path}.${key}` : key,
          expected: 'not present',
          actual: 'present',
          message: `Extra field in simulator: ${key}`,
        });
      }
    }
  }

  // Compare array element types
  if (expected.type === 'array' && actual.type === 'array') {
    if (expected.arrayElementType && actual.arrayElementType) {
      // If either side has 'unknown' type (empty array), skip comparison
      // We can't validate schema when there's no data
      if (expected.arrayElementType.type !== 'unknown' && actual.arrayElementType.type !== 'unknown') {
        const nestedResult = compareSchemas(
          expected.arrayElementType,
          actual.arrayElementType,
          `${path}[]`
        );
        differences.push(...nestedResult.differences);
      }
    }
  }

  return {
    match: differences.filter(d => !d.message.includes('Extra field')).length === 0,
    differences,
  };
}

/**
 * Format schema comparison result for display
 */
export function formatComparisonResult(result: SchemaComparisonResult): string {
  if (result.match && result.differences.length === 0) {
    return 'Schemas match exactly';
  }

  const lines: string[] = [];

  if (result.match) {
    lines.push('Schemas match (with minor differences):');
  } else {
    lines.push('Schema mismatch:');
  }

  for (const diff of result.differences) {
    lines.push(`  ${diff.path}: ${diff.message}`);
  }

  return lines.join('\n');
}

/**
 * Get a list of all field paths in a schema
 */
export function getFieldPaths(schema: SchemaNode, prefix = ''): string[] {
  const paths: string[] = [];

  if (schema.type === 'object' && schema.fields) {
    for (const [key, value] of Object.entries(schema.fields)) {
      const path = prefix ? `${prefix}.${key}` : key;
      paths.push(path);
      paths.push(...getFieldPaths(value, path));
    }
  } else if (schema.type === 'array' && schema.arrayElementType) {
    paths.push(...getFieldPaths(schema.arrayElementType, `${prefix}[]`));
  }

  return paths;
}
