/**
 * Integration tests for Polygon IPOs API
 * Tests: /vX/reference/ipos
 */

import { polygonClient, simulatorClient, getSimDate, expectResultsNotEmpty, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

function scrubFutureIpoDates(data: unknown, cutoffDate: string): unknown {
  if (!data || typeof data !== 'object') return data;
  const clone = JSON.parse(JSON.stringify(data)) as { results?: Array<Record<string, unknown>> };
  if (!Array.isArray(clone.results)) return clone;
  const cutoff = new Date(`${cutoffDate}T00:00:00Z`).getTime();
  const dateKeys = [
    'last_updated',
    'listing_date',
    'announced_date',
    'issue_start_date',
    'issue_end_date',
  ];
  for (const item of clone.results) {
    for (const key of dateKeys) {
      const value = item[key];
      if (typeof value !== 'string') continue;
      const ts = Date.parse(value);
      if (!Number.isNaN(ts) && ts > cutoff) {
        delete item[key];
      }
    }
  }
  return clone;
}

describe('Polygon IPOs API', () => {
  describe('GET /vX/reference/ipos', () => {
    it('should return matching schema for IPOs list', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getIpos({
        'listing_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getIpos({
        'listing_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('IPOs list polygon', polygonResponse.data);
      expectResultsNotEmpty('IPOs list simulator', simulatorResponse.data);

      const polygonData = scrubFutureIpoDates(polygonResponse.data, cutoffDate);
      const polygonSchema = extractSchema(polygonData);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs list',
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

    it('should handle ipo_status=history parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getIpos({
        ipo_status: 'history',
        'listing_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getIpos({
        ipo_status: 'history',
        'listing_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('IPOs ipo_status=history polygon', polygonResponse.data);
      expectResultsNotEmpty('IPOs ipo_status=history simulator', simulatorResponse.data);

      const polygonData = scrubFutureIpoDates(polygonResponse.data, cutoffDate);
      const polygonSchema = extractSchema(polygonData);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs ipo_status=history',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle announced_date.gte parameter', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getIpos({
        'announced_date.gte': '2015-01-01',
        'listing_date.lte': cutoffDate,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getIpos({
        'announced_date.gte': '2015-01-01',
        'listing_date.lte': cutoffDate,
        limit: 10,
      });

      expectResultsNotEmpty('IPOs announced_date.gte polygon', polygonResponse.data);
      expectResultsNotEmpty('IPOs announced_date.gte simulator', simulatorResponse.data);

      const polygonData = scrubFutureIpoDates(polygonResponse.data, cutoffDate);
      const polygonSchema = extractSchema(polygonData);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs announced_date.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const cutoffDate = getSimDate();
      const polygonResponse = await polygonClient.getIpos({
        'listing_date.lte': cutoffDate,
        limit: 1,
      });
      const simulatorResponse = await simulatorClient.getIpos({
        'listing_date.lte': cutoffDate,
        limit: 1,
      });

      expectResultsNotEmpty('IPOs pagination polygon', polygonResponse.data);
      expectResultsNotEmpty('IPOs pagination simulator', simulatorResponse.data);

      const polygonData = scrubFutureIpoDates(polygonResponse.data, cutoffDate);
      const polygonSchema = extractSchema(polygonData);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'IPOs pagination next_url',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
      expect(polygonHasNext).toBe(simulatorHasNext);
    });
  });
});
