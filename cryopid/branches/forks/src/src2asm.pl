#!/usr/bin/perl -w

print <<EOT;
.sect .data
.global stub_data
.global stub_length

.align 2 /* for a 32-bit boundary */
stub_data:
EOT
my $size = 0;
my $a;
while (read STDIN,$a,1) {
	printf ".byte \%d\n", ord($a);
	$size++;
}
print <<EOT;

	.align 2
stub_length:
	.long $size
EOT
