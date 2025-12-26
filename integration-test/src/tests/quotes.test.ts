/**
 * Integration tests for Polygon Quotes API
 * Tests: /v3/quotes/{symbol}, /v2/last/nbbo/{symbol}
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Quotes API', () => {
  const testDate = config.testStartDate;

  describe('GET /v3/quotes/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s quotes',
        async (symbol) => {
          const polygonResponse = await polygonClient.getQuotes(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getQuotes(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Quotes ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });

    describe('Optional Parameters', () => {
      it('should handle timestamp.gte parameter', async () => {
        const symbol = config.testSymbols[0];
        const timestamp = `${testDate}T09:30:00Z`;

        const polygonResponse = await polygonClient.getQuotes(symbol, {
          'timestamp.gte': timestamp,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getQuotes(symbol, {
          'timestamp.gte': timestamp,
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Quotes timestamp.gte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timestamp.lte parameter', async () => {
        const symbol = config.testSymbols[0];
        const timestamp = `${testDate}T16:00:00Z`;

        const polygonResponse = await polygonClient.getQuotes(symbol, {
          'timestamp.lte': timestamp,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getQuotes(symbol, {
          'timestamp.lte': timestamp,
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Quotes timestamp.lte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=asc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getQuotes(symbol, {
          order: 'asc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getQuotes(symbol, {
          order: 'asc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Quotes order=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=desc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getQuotes(symbol, {
          order: 'desc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getQuotes(symbol, {
          order: 'desc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Quotes order=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getQuotes(symbol, { limit: 5 });
        const simulatorResponse = await simulatorClient.getQuotes(symbol, { limit: 5 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Quotes limit=5',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/last/nbbo/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s last quote',
        async (symbol) => {
          const polygonResponse = await polygonClient.getLastQuote(symbol);
          const simulatorResponse = await simulatorClient.getLastQuote(symbol);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Last quote ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });
  });
});
