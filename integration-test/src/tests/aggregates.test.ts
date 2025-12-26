/**
 * Integration tests for Polygon Aggregates (Bars) API
 * Tests: /v2/aggs/ticker/{symbol}/range/..., /v2/aggs/ticker/{symbol}/prev, grouped daily
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Aggregates API', () => {
  const testDate = config.testStartDate;
  const testEndDate = config.testEndDate;

  describe('GET /v2/aggs/ticker/{symbol}/range/{multiplier}/{timespan}/{from}/{to}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s with 1 day timespan',
        async (symbol) => {
          const polygonResponse = await polygonClient.getAggregateBars(
            symbol,
            1,
            'day',
            testDate,
            testEndDate
          );

          const simulatorResponse = await simulatorClient.getAggregateBars(
            symbol,
            1,
            'day',
            testDate,
            testEndDate
          );

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Aggregate bars ${symbol}`,
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

      it.each(config.testSymbols)(
        'should return matching schema for %s with 1 hour timespan',
        async (symbol) => {
          const polygonResponse = await polygonClient.getAggregateBars(
            symbol,
            1,
            'hour',
            testDate,
            testEndDate
          );

          const simulatorResponse = await simulatorClient.getAggregateBars(
            symbol,
            1,
            'hour',
            testDate,
            testEndDate
          );

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Aggregate bars hourly ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );

      it.each(config.testSymbols)(
        'should return matching schema for %s with 1 minute timespan',
        async (symbol) => {
          const polygonResponse = await polygonClient.getAggregateBars(
            symbol,
            1,
            'minute',
            testDate,
            testDate
          );

          const simulatorResponse = await simulatorClient.getAggregateBars(
            symbol,
            1,
            'minute',
            testDate,
            testDate
          );

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Aggregate bars minute ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });

    describe('Optional Parameters', () => {
      it('should handle adjusted=true parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { adjusted: true }
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { adjusted: true }
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars adjusted=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle adjusted=false parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { adjusted: false }
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { adjusted: false }
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars adjusted=false',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle sort=asc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { sort: 'asc' }
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { sort: 'asc' }
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars sort=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle sort=desc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { sort: 'desc' }
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          1,
          'day',
          testDate,
          testEndDate,
          { sort: 'desc' }
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars sort=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          1,
          'minute',
          testDate,
          testDate,
          { limit: 10 }
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          1,
          'minute',
          testDate,
          testDate,
          { limit: 10 }
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars limit=10',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });

    describe('Different Multipliers', () => {
      it('should handle 5 minute bars', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          5,
          'minute',
          testDate,
          testDate
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          5,
          'minute',
          testDate,
          testDate
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars 5-minute',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle 15 minute bars', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getAggregateBars(
          symbol,
          15,
          'minute',
          testDate,
          testDate
        );

        const simulatorResponse = await simulatorClient.getAggregateBars(
          symbol,
          15,
          'minute',
          testDate,
          testDate
        );

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Aggregate bars 15-minute',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/aggs/ticker/{symbol}/prev', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s previous close',
        async (symbol) => {
          const polygonResponse = await polygonClient.getPreviousClose(symbol);
          const simulatorResponse = await simulatorClient.getPreviousClose(symbol);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Previous close ${symbol}`,
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
      it('should handle adjusted=true parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getPreviousClose(symbol, { adjusted: true });
        const simulatorResponse = await simulatorClient.getPreviousClose(symbol, { adjusted: true });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Previous close adjusted=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle adjusted=false parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getPreviousClose(symbol, { adjusted: false });
        const simulatorResponse = await simulatorClient.getPreviousClose(symbol, { adjusted: false });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Previous close adjusted=false',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/aggs/grouped/locale/us/market/stocks/{date}', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for grouped daily', async () => {
        const polygonResponse = await polygonClient.getGroupedDaily(testDate);
        const simulatorResponse = await simulatorClient.getGroupedDaily(testDate);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Grouped daily',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        if (!comparison.match) {
          console.log(formatComparisonResult(comparison));
        }

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });

    describe('Optional Parameters', () => {
      it('should handle adjusted=true parameter', async () => {
        const polygonResponse = await polygonClient.getGroupedDaily(testDate, { adjusted: true });
        const simulatorResponse = await simulatorClient.getGroupedDaily(testDate, { adjusted: true });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Grouped daily adjusted=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle include_otc=false parameter', async () => {
        const polygonResponse = await polygonClient.getGroupedDaily(testDate, { include_otc: false });
        const simulatorResponse = await simulatorClient.getGroupedDaily(testDate, { include_otc: false });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Grouped daily include_otc=false',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });
});
