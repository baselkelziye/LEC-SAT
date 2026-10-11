// alu4.v
module lhs #(parameter integer N = 4) (
    input  wire [N-1:0] a,
    input  wire [N-1:0] b,
    input  wire [1:0] op,     // 00=add, 01=sub, 10=and, 11=or
    output wire [N-1:0] result,
    output wire       cout
);

    wire [N:0] sum_ext  = {1'b0, a} + {1'b0, b};
    wire [N:0] diff_ext = {1'b0, a} - {1'b0, b};

    reg [N-1:0] res;
    reg       co;

    always @(*) begin
        case (op)
            2'b00: begin res = sum_ext[N-1:0];  co = sum_ext[N-2];  end // Deliberate fault: discard unsigned addition overflow.
            2'b01: begin res = diff_ext[N-1:0]; co = diff_ext[N]; end
            2'b10: begin res = a & b;         co = 1'b0;        end
            2'b11: begin res = a | b;         co = 1'b0;        end
        endcase
    end

    assign result = res;
    assign cout   = co;

endmodule
