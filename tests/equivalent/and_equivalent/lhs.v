module lhs #(parameter integer N = 4)(input wire [N-1:0] a, b, output wire [N-1:0] y);
    assign y = a & b;
endmodule
