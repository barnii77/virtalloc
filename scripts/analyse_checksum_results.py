from collections import Counter
from statistics import mean, median, stdev
import sys

def analyze_checksums(checksums):
    # Count occurrences of each checksum
    checksum_counts = Counter(checksums)

    # Basic statistics
    total_checksums = len(checksums)
    unique_checksums = len(checksum_counts)
    occurrences = list(checksum_counts.values())

    # Calculate collision metrics
    max_collisions = max(occurrences)
    avg_occurrences = mean(occurrences)
    median_occurrences = median(occurrences)

    try:
        std_dev_occurrences = stdev(occurrences)
    except:
        std_dev_occurrences = 0

    # Find checksums with most collisions
    most_common = checksum_counts.most_common(5)

    # Count checksums by number of occurrences
    occurrence_distribution = Counter(occurrences)

    # Calculate percentage of checksums that have collisions
    checksums_with_collisions = sum(1 for count in occurrences if count > 1)
    collision_percentage = (checksums_with_collisions / unique_checksums) * 100

    # Print results
    print("\n=== Checksum Analysis Results ===")
    print(f"Total checksums processed: {total_checksums:,}")
    print(f"Unique checksums found: {unique_checksums:,}")
    print(f"Duplicate ratio: {total_checksums/unique_checksums:.2f}")
    print("\n=== Collision Statistics ===")
    print(f"Maximum collisions: {max_collisions}")
    print(f"Average occurrences per unique checksum: {avg_occurrences:.2f}")
    print(f"Median occurrences per unique checksum: {median_occurrences}")
    print(f"Standard deviation of occurrences: {std_dev_occurrences:.2f}")
    print(f"Percentage of checksums with collisions: {collision_percentage:.2f}%")

    print("\n=== Top 5 Most Common Checksums ===")
    for checksum, count in most_common:
        print(f"Checksum: {checksum}, Occurrences: {count}")

    print("\n=== Occurrence Distribution ===")
    for count in sorted(occurrence_distribution.keys()):
        num_checksums = occurrence_distribution[count]
        print(f"Checksums occurring {count} times: {num_checksums:,}")

def main():
    # Read checksums from stdin, one per line
    checksums = [line.strip() for line in sys.stdin]
    analyze_checksums(checksums)

if __name__ == "__main__":
    main()